#include "dg/llvm/PointerAnalysis/PointerGraph.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace pta {

// create subgraph or add edges to already existing subgraph,
// return the CALL node (the first) and the RETURN node (the second),
// so that we can connect them into the PointerGraph
LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::createCall(const llvm::Instruction *Inst) {
    using namespace llvm;

    const CallInst *CInst = cast<CallInst>(Inst);

#if LLVM_VERSION_MAJOR >= 8
    const Value *calledVal = CInst->getCalledOperand()->stripPointerCasts();
#else
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
#endif

    if (CInst->isInlineAsm()) {
        return createAsm(Inst);
    }

    if (const Function *func = dyn_cast<Function>(calledVal)) {
        if (func->isDeclaration()) {
            return addNode(CInst, createUndefFunctionCall(CInst, func));
        }
        return createCallToFunction(CInst, func);
    } // this is a function pointer call
    return createFuncptrCall(CInst, calledVal);
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createUndefFunctionCall(const llvm::CallInst *CInst,
                                                 const llvm::Function *func) {
    assert(func->empty());
    // is it a call to free? If so, create invalidate node instead.
    if (invalidate_nodes && func->getName().equals("free")) {
        return {createFree(CInst)};
    }
    if (threads_) {
        if (func->getName().equals("pthread_create")) {
            return createPthreadCreate(CInst);
        }
        if (func->getName().equals("pthread_join")) {
            return createPthreadJoin(CInst);
        }
        if (func->getName().equals("pthread_exit")) {
            return createPthreadExit(CInst);
        }
    }
    /// memory allocation (malloc, calloc, etc.)
    auto type = _options.getAllocationFunction(func->getName().str());
    if (type != AllocationFunction::NONE) {
        return createDynamicMemAlloc(CInst, type);
    }
    if (func->isIntrinsic()) {
        return createIntrinsic(CInst);
    }

    // mempy/memmove
    const auto &funname = func->getName();
    if (funname.equals("memcpy") || funname.equals("__memcpy_chk") ||
        funname.equals("memove")) {
        auto *dest = CInst->getOperand(0);
        auto *src = CInst->getOperand(1);
        auto lenVal = llvmutils::getConstantValue(CInst->getOperand(2));
        return PS.create<PSNodeType::MEMCPY>(getOperand(src), getOperand(dest),
                                             lenVal);
    }

    return {createUnknownCall()};
}

LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::createFuncptrCall(const llvm::CallInst *CInst,
                                           const llvm::Value *calledVal) {
    // just the call_funcptr and call_return nodes are created and
    // when the pointers are resolved during analysis, the graph
    // will be dynamically created and it will replace these nodes
    PSNode *op = getOperand(calledVal);
    PSNode *call_funcptr = PS.create<PSNodeType::CALL_FUNCPTR>(op);
    PSNode *ret_call = PS.create<PSNodeType::CALL_RETURN>();

    ret_call->setPairedNode(call_funcptr);
    call_funcptr->setPairedNode(ret_call);

    call_funcptr->setUserData(const_cast<llvm::CallInst *>(CInst));

    return addNode(CInst, {call_funcptr, ret_call});
}

PSNode *LLVMPointerGraphBuilder::createUnknownCall() {
    // This assertion must not hold if the call is wrapped
    // inside bitcast - it defaults to int, but is bitcased
    // to pointer
    // assert(CInst->getType()->isPointerTy());
    PSNode *call = PS.create<PSNodeType::CALL>();
    call->setPairedNode(call);

    // the only thing that the node will point at
    call->addPointsTo(UnknownPointer);

    return call;
}

PSNode *
LLVMPointerGraphBuilder::createMemTransfer(const llvm::IntrinsicInst *I) {
    using namespace llvm;
    const Value *dest, *src; //, *lenVal;
    uint64_t lenVal = Offset::UNKNOWN;

    switch (I->getIntrinsicID()) {
    case Intrinsic::memmove:
    case Intrinsic::memcpy:
        dest = I->getOperand(0);
        src = I->getOperand(1);
        lenVal = llvmutils::getConstantValue(I->getOperand(2));
        break;
    default:
        errs() << "ERR: unhandled mem transfer intrinsic" << *I << "\n";
        abort();
    }

    PSNode *destNode = getOperand(dest);
    PSNode *srcNode = getOperand(src);
    PSNode *node = PS.create<PSNodeType::MEMCPY>(srcNode, destNode, lenVal);

    return node;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createMemSet(const llvm::Instruction *Inst) {
    PSNode *val;
    if (llvmutils::memsetIsZeroInitialization(
                llvm::cast<llvm::IntrinsicInst>(Inst)))
        val = NULLPTR;
    else
        // if the memset is not 0-initialized, it does some
        // garbage into the pointer
        val = UNKNOWN_MEMORY;

    PSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    // we need to make unknown offsets
    PSNode *G = PS.create<PSNodeType::GEP>(op, Offset::UNKNOWN);
    PSNode *S = PS.create<PSNodeType::STORE>(val, G);

    PSNodesSeq ret(G);
    ret.append(S);
    // no representant here...

    return ret;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createVarArg(const llvm::IntrinsicInst *Inst) {
    // just store all the pointers from vararg argument
    // to the memory given in vastart() on Offset::UNKNOWN.
    // It is the easiest thing we can do without any further
    // analysis
    PSNodesSeq ret;

    // first we need to get the vararg argument phi
    const llvm::Function *F = Inst->getParent()->getParent();
    PointerSubgraph *subg = subgraphs_map[F];
    assert(subg);
    PSNode *arg = subg->vararg;
    assert(F->isVarArg() && "vastart in a non-variadic function");
    assert(arg && "Don't have variadic argument in a variadic function");

    // vastart will be node that will keep the memory
    // with pointers, its argument is the alloca, that
    // alloca will keep pointer to vastart
    PSNode *vastart = PS.create<PSNodeType::ALLOC>();

    // vastart has only one operand which is the struct
    // it uses for storing the va arguments. Strip it so that we'll
    // get the underlying alloca inst
    PSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    // the argument is usually an alloca, but it may be a load
    // in the case the code was transformed by -reg2mem
    assert((op->getType() == PSNodeType::ALLOC ||
            op->getType() == PSNodeType::LOAD) &&
           "Argument of vastart is invalid");
    // get node with the same pointer, but with Offset::UNKNOWN
    // FIXME: we're leaking it
    // make the memory in alloca point to our memory in vastart
    PSNode *ptr = PS.create<PSNodeType::GEP>(op, Offset::UNKNOWN);
    PSNode *S1 = PS.create<PSNodeType::STORE>(vastart, ptr);
    // and also make vastart point to the vararg args
    PSNode *S2 = PS.create<PSNodeType::STORE>(arg, vastart);

    ret.append(vastart);
    ret.append(ptr);
    ret.append(S1);
    ret.append(S2);

    ret.setRepresentant(vastart);

    return ret;
}

PSNode *
LLVMPointerGraphBuilder::createLifetimeEnd(const llvm::Instruction *Inst) {
    PSNode *op1 = getOperand(Inst->getOperand(1));
    return PS.create<PSNodeType::INVALIDATE_OBJECT>(op1);
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createIntrinsic(const llvm::Instruction *Inst) {
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(Inst);
    if (isa<MemTransferInst>(I)) {
        return createMemTransfer(I);
    }
    if (isa<MemSetInst>(I)) {
        return createMemSet(I);
    }

    switch (I->getIntrinsicID()) {
    case Intrinsic::vastart:
        return createVarArg(I);
    case Intrinsic::stacksave:
        errs() << "WARNING: Saving stack may yield unsound results!: " << *Inst
               << "\n";
        return {PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>())};
    case Intrinsic::stackrestore:
        return {createInternalLoad(Inst)};
    case Intrinsic::lifetime_end:
        return {createLifetimeEnd(Inst)};
    default:
        errs() << *Inst << "\n";
        errs() << "Unhandled intrinsic ^^\n";
        abort();
    }
}

LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::createAsm(const llvm::Instruction *Inst) {
    // we filter irrelevant calls in isRelevantCall()
    // and we don't have assembler there at all. If
    // we are here, then we got here because this
    // is undefined call that returns pointer.
    // In this case return an unknown pointer
    static bool warned = false;
    if (!warned) {
        llvm::errs()
                << "PTA: Inline assembly found, analysis  may be unsound\n";
        warned = true;
    }

    PSNode *n =
            PS.create<PSNodeType::CONSTANT>(UNKNOWN_MEMORY, Offset::UNKNOWN);
    // it is call that returns pointer, so we'd like to have
    // a 'return' node that contains that pointer
    n->setPairedNode(n);
    return addNode(Inst, n);
}

PSNode *LLVMPointerGraphBuilder::createFree(const llvm::Instruction *Inst) {
    PSNode *op1 = getOperand(Inst->getOperand(0));
    PSNode *node = PS.create<PSNodeType::FREE>(op1);

    return node;
}

PSNode *LLVMPointerGraphBuilder::createDynamicAlloc(const llvm::CallInst *CInst,
                                                    AllocationFunction type) {
    using namespace llvm;

    const Value *op;
    uint64_t size = 0, size2 = 0;
    PSNodeAlloc *node = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    node->setIsHeap();

    switch (type) {
    case AllocationFunction::MALLOC:
        node->setIsHeap();
        /* fallthrough */
    case AllocationFunction::ALLOCA:
        op = CInst->getOperand(0);
        break;
    case AllocationFunction::CALLOC:
        node->setIsHeap();
        node->setZeroInitialized();
        op = CInst->getOperand(1);
        break;
    default:
        errs() << *CInst << "\n";
        assert(0 && "unknown memory allocation type");
        // for NDEBUG
        abort();
    };

    // infer allocated size
    size = llvmutils::getConstantSizeValue(op);
    if (size != 0 && type == AllocationFunction::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = llvmutils::getConstantSizeValue(CInst->getOperand(0));
        // if both ops are constants, multiply them to get
        // the correct size, otherwise return 0 (unknown)
        size = size2 != 0 ? size * size2 : 0;
    }

    node->setSize(size);
    return node;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createRealloc(const llvm::CallInst *CInst) {
    using namespace llvm;

    PSNodesSeq ret;

    // we create new allocation node and memcpy old pointers there
    PSNode *orig_mem = getOperand(CInst->getOperand(0));
    PSNodeAlloc *reall = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    reall->setIsHeap();
    reall->setUserData(const_cast<llvm::CallInst *>(CInst));

    // copy everything that is in orig_mem to reall
    PSNode *mcp =
            PS.create<PSNodeType::MEMCPY>(orig_mem, reall, Offset::UNKNOWN);
    // we need the pointer in the last node that we return
    PSNode *ptr = PS.create<PSNodeType::CONSTANT>(reall, 0);

    reall->setIsHeap();
    reall->setSize(llvmutils::getConstantSizeValue(CInst->getOperand(1)));

    ret.append(reall);
    ret.append(mcp);
    ret.append(ptr);
    ret.setRepresentant(ptr);

    return ret;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createDynamicMemAlloc(const llvm::CallInst *CInst,
                                               AllocationFunction type) {
    assert(type != AllocationFunction::NONE &&
           "BUG: creating dyn. memory node for NONMEM");

    PSNodesSeq seq;
    if (type == AllocationFunction::REALLOC) {
        seq = createRealloc(CInst);
    } else {
        seq = {createDynamicAlloc(CInst, type)};
    }
    return seq;
}

} // namespace pta
} // namespace dg
