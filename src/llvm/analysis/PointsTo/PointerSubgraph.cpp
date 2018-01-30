#include <cassert>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_os_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "analysis/PointsTo/PointerSubgraph.h"
#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

using dg::MemAllocationFuncs;

static inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

static uint64_t getConstantValue(const llvm::Value *op)
{
    using namespace llvm;

    uint64_t size = 0;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
        // if the size cannot be expressed as an uint64_t,
        // just set it to 0 (that means unknown)
        if (size == ~((uint64_t) 0))
            size = 0;
    }

    return size;
}

static uint64_t getAllocatedSize(const llvm::AllocaInst *AI,
                                 const llvm::DataLayout *DL)
{
    llvm::Type *Ty = AI->getAllocatedType();
    if (!Ty->isSized())
            return 0;

    if (AI->isArrayAllocation())
        return getConstantValue(AI->getArraySize()) * DL->getTypeAllocSize(Ty);
    else
        return DL->getTypeAllocSize(Ty);
}

bool LLVMPointerSubgraphBuilder::typeCanBePointer(llvm::Type *Ty) const
{
    if (Ty->isPointerTy())
        return true;

    if (Ty->isIntegerTy() && Ty->isSized())
        return DL->getTypeSizeInBits(Ty)
                >= DL->getPointerSizeInBits(/*Ty->getPointerAddressSpace()*/);

    return false;
}

LLVMPointerSubgraphBuilder::~LLVMPointerSubgraphBuilder()
{
    delete DL;
}

Pointer LLVMPointerSubgraphBuilder::handleConstantPtrToInt(const llvm::PtrToIntInst *P2I)
{
    using namespace llvm;

    const Value *llvmOp = P2I->getOperand(0);
    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant PtrToInt with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantIntToPtr(const llvm::IntToPtrInst *I2P)
{
    using namespace llvm;

    const Value *llvmOp = I2P->getOperand(0);
    if (isa<ConstantInt>(llvmOp)) {
        llvm::errs() << "IntToPtr with constant: " << *I2P << "\n";
        return PointerUnknown;
    }

    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant PtrToInt with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantAdd(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *op;
    const Value *val = nullptr;
    uint64_t off = UNKNOWN_OFFSET;

    // see createAdd() for details
    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
        val = Inst->getOperand(0);
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
        val = Inst->getOperand(1);
    } else {
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    if (val)
        off = getConstantValue(val);

    assert(op->pointsTo.size() == 1
           && "Constant add with not only one pointer");

    Pointer ptr = *op->pointsTo.begin();
    if (off)
        return Pointer(ptr.target, *ptr.offset + off);
    else
        return Pointer(ptr.target, UNKNOWN_OFFSET);
}

Pointer LLVMPointerSubgraphBuilder::handleConstantArithmetic(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *op;

    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
    } else {
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    assert(op->pointsTo.size() == 1
           && "Constant add with not only one pointer");

    Pointer ptr = *op->pointsTo.begin();
    return Pointer(ptr.target, UNKNOWN_OFFSET);
}

Pointer LLVMPointerSubgraphBuilder::handleConstantBitCast(const llvm::BitCastInst *BC)
{
    using namespace llvm;

    if (!BC->isLosslessCast()) {
        errs() << "WARN: Not a loss less cast unhandled ConstExpr"
               << *BC << "\n";
        abort();
        return PointerUnknown;
    }

    const Value *llvmOp = BC->stripPointerCasts();
    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant BitCast with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantGep(const llvm::GetElementPtrInst *GEP)
{
    using namespace llvm;

    const Value *op = GEP->getPointerOperand();
    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    // get operand PSNode (this may result in recursive call,
    // if this gep is recursively defined)
    PSNode *opNode = getOperand(op);
    assert(opNode->pointsTo.size() == 1
           && "Constant node has more that 1 pointer");
    pointer = *(opNode->pointsTo.begin());

    unsigned bitwidth = getPointerBitwidth(DL, op);
    APInt offset(bitwidth, 0);

    // get offset of this GEP
    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth) && !pointer.offset.isUnknown())
            pointer.offset = offset.getZExtValue();
        else
            errs() << "WARN: Offset greater than "
                   << bitwidth << "-bit" << *GEP << "\n";
    }

    return pointer;
}

Pointer LLVMPointerSubgraphBuilder::getConstantExprPointer(const llvm::ConstantExpr *CE)
{
    using namespace llvm;

    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
    Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();

    switch(Inst->getOpcode()) {
        case Instruction::GetElementPtr:
            pointer = handleConstantGep(cast<GetElementPtrInst>(Inst));
            break;
        //case Instruction::ExtractValue:
        //case Instruction::Select:
            break;
        case Instruction::BitCast:
        case Instruction::SExt:
        case Instruction::ZExt:
            pointer = handleConstantBitCast(cast<BitCastInst>(Inst));
            break;
        case Instruction::PtrToInt:
            pointer = handleConstantPtrToInt(cast<PtrToIntInst>(Inst));
            break;
        case Instruction::IntToPtr:
            pointer = handleConstantIntToPtr(cast<IntToPtrInst>(Inst));
            break;
        case Instruction::Add:
            pointer = handleConstantAdd(Inst);
            break;
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Trunc:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
            pointer = PointerUnknown;
            break;
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::SDiv:
            pointer = handleConstantArithmetic(Inst);
            break;
        default:
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

#if LLVM_VERSION_MAJOR < 5
    delete Inst;
#else
    Inst->deleteValue();
#endif
    return pointer;
}

PSNode *LLVMPointerSubgraphBuilder::createConstantExpr(const llvm::ConstantExpr *CE)
{
    Pointer ptr = getConstantExprPointer(CE);
    PSNode *node = PS.create(PSNodeType::CONSTANT, ptr.target, ptr.offset);

    addNode(CE, node);

    assert(node);
    return node;
}

static bool isConstantZero(const llvm::Value *val)
{
    using namespace llvm;

    if (const ConstantInt *C = dyn_cast<ConstantInt>(val))
        return C->isZero();

    return false;
}

PSNode *LLVMPointerSubgraphBuilder::getConstant(const llvm::Value *val)
{
    if (llvm::isa<llvm::ConstantPointerNull>(val)
        || isConstantZero(val)) {
        return NULLPTR;
    } else if (llvm::isa<llvm::UndefValue>(val)) {
        return UNKNOWN_MEMORY;
    } else if (const llvm::ConstantExpr *CE
                    = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
        return createConstantExpr(CE);
    } else if (llvm::isa<llvm::Function>(val)) {
        PSNode *ret = PS.create(PSNodeType::FUNCTION);
        addNode(val, ret);
        return ret;
    } else if (llvm::isa<llvm::Constant>(val)) {
        // it is just some constant that we can not handle
        return UNKNOWN_MEMORY;
    } else
        return nullptr;
}

// try get operand, return null if no such value has been constructed
PSNode *LLVMPointerSubgraphBuilder::tryGetOperand(const llvm::Value *val)
{
    auto it = nodes_map.find(val);
    PSNode *op = nullptr;

    if (it != nodes_map.end())
        op = it->second.second;

    // if we don't have the operand, then it is a ConstantExpr
    // or some operand of intToPtr instruction (or related to that)
    if (!op) {
        if (llvm::isa<llvm::Constant>(val)) {
            op = getConstant(val);
            if (!op) {
                // unknown constant
                llvm::errs() << "ERR: unhandled constant: " << *val << "\n";
                return nullptr;
            }
        } else
            // unknown operand
            return nullptr;
    }

    // we either found the operand, or we bailed out earlier,
    // so we need to have the operand here
    assert(op && "Did not find an operand");

    // if the operand is a call, use the return node of the call instead
    // - that is the one that contains returned pointers
    if (op->getType() == PSNodeType::CALL
        || op->getType() == PSNodeType::CALL_FUNCPTR) {
        op = op->getPairedNode();
    }

    return op;
}

PSNode *LLVMPointerSubgraphBuilder::buildNode(const llvm::Value *val)
{

    assert(nodes_map.count(val) == 0);

    const llvm::Instruction *Inst
        = llvm::dyn_cast<llvm::Instruction>(val);

    if (Inst) {
            PSNodesSeq seq = buildInstruction(*Inst);
            assert(seq.first && seq.second);
            return seq.second;
    } else if (const llvm::Argument *A
                = llvm::dyn_cast<llvm::Argument>(val)) {
        return createArgument(A);
    } else {
        // this may happen when C code is corrupted like this:
        // int a, b;
        // a = &b;
        // a = 3;
        //
        // 'a' is int but is assigned an address of 'b', which leads
        // to creating an inttoptr/ptrtoint instructions that
        // have forexample 'i32 3' as operand
        llvm::errs() << "Invalid value leading to UNKNOWN: " << *val << "\n";
        return createUnknown(val);
    }
}

static bool isRelevantIntrinsic(const llvm::Function *func)
{
    using namespace llvm;

    switch (func->getIntrinsicID()) {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::vastart:
        case Intrinsic::stacksave:
        case Intrinsic::stackrestore:
            return true;
        // case Intrinsic::memset:
        default:
            return false;
    }
}

static bool isInvalid(const llvm::Value *val)
{
    using namespace llvm;

    if (!isa<Instruction>(val)) {
        if (!isa<Argument>(val) && !isa<GlobalValue>(val))
            return true;
    } else {
        if (isa<ICmpInst>(val) || isa<FCmpInst>(val)
            || isa<DbgValueInst>(val) || isa<BranchInst>(val)
            || isa<SwitchInst>(val))
            return true;

        const CallInst *CI = dyn_cast<CallInst>(val);
        if (CI) {
            const Function *F = CI->getCalledFunction();
            if (F && F->isIntrinsic() && !isRelevantIntrinsic(F))
                return true;
        }
    }

    return false;
}

PSNode *LLVMPointerSubgraphBuilder::getOperand(const llvm::Value *val)
{
    PSNode *op = tryGetOperand(val);
    if (!op) {
        if (isInvalid(val))
            return UNKNOWN_MEMORY;
        else
            return buildNode(val);
    } else
        return op;
}

PSNode *LLVMPointerSubgraphBuilder::createDynamicAlloc(const llvm::CallInst *CInst, MemAllocationFuncs type)
{
    using namespace llvm;

    const Value *op;
    uint64_t size = 0, size2 = 0;
    PSNodeAlloc *node = PSNodeAlloc::get(PS.create(PSNodeType::DYN_ALLOC));

    switch (type) {
        case MemAllocationFuncs::MALLOC:
            node->setIsHeap();
            /* fallthrough */
        case MemAllocationFuncs::ALLOCA:
            op = CInst->getOperand(0);
            break;
        case MemAllocationFuncs::CALLOC:
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
    size = getConstantValue(op);
    if (size != 0 && type == MemAllocationFuncs::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node->setSize(size);
    return node;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createRealloc(const llvm::CallInst *CInst)
{
    using namespace llvm;

    // we create new allocation node and memcpy old pointers there
    PSNode *orig_mem = getOperand(CInst->getOperand(0)->stripInBoundsOffsets());
    PSNodeAlloc *reall = PSNodeAlloc::get(PS.create(PSNodeType::DYN_ALLOC));
    // copy everything that is in orig_mem to reall
    PSNode *mcp = PS.create(PSNodeType::MEMCPY, orig_mem, reall, 0, UNKNOWN_OFFSET);
    // we need the pointer in the last node that we return
    PSNode *ptr = PS.create(PSNodeType::CONSTANT, reall, 0);

    reall->setIsHeap();
    reall->setSize(getConstantValue(CInst->getOperand(1)));

    reall->addSuccessor(mcp);
    mcp->addSuccessor(ptr);

    reall->setUserData(const_cast<llvm::CallInst *>(CInst));

    PSNodesSeq ret = PSNodesSeq(reall, ptr);
    addNode(CInst, ptr);

    return ret;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createDynamicMemAlloc(const llvm::CallInst *CInst, MemAllocationFuncs type)
{
    assert(type != MemAllocationFuncs::NONEMEM
            && "BUG: creating dyn. memory node for NONMEM");

    if (type == MemAllocationFuncs::REALLOC) {
        return createRealloc(CInst);
    } else {
        PSNode *node = createDynamicAlloc(CInst, type);
        addNode(CInst, node);

        // we return (node, node), so that the parent function
        // will seamlessly connect this node into the graph
        return std::make_pair(node, node);
    }
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createCallToFunction(const llvm::Function *F)
{
    PSNode *callNode, *returnNode;

    // the operands to the return node (which works as a phi node)
    // are going to be added when the subgraph is built
    callNode = PS.create(PSNodeType::CALL, nullptr);
    returnNode = PS.create(PSNodeType::CALL_RETURN, nullptr);

    returnNode->setPairedNode(callNode);
    callNode->setPairedNode(returnNode);

    // reuse built subgraphs if available
    Subgraph& subg = subgraphs_map[F];
    if (!subg.root) {
        // create a new subgraph
        buildFunction(*F);
    }

    // we took the subg by reference, so it should be filled now
    assert(subg.root && subg.ret);

    // add an edge from last argument to root of the subgraph
    // and from the subprocedure return node (which is one - unified
    // for all return nodes) to return from the call
    callNode->addSuccessor(subg.root);
    subg.ret->addSuccessor(returnNode);

    // handle value returned from the function if it is a pointer
    // DONT: if (CInst->getType()->isPointerTy()) {
    // we need to handle the return values even when it is not
    // a pointer as we have ptrtoint and inttoptr

    // create the pointer arguments -- the other arguments will
    // be created later if needed
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A)
        getOperand(&*A);

    return std::make_pair(callNode, returnNode);
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createFuncptrCall(const llvm::CallInst *CInst,
                                              const llvm::Function *F)
{
    // set this flag to true, so that createCallToFunction
    // will also add the program structure instead of only
    // building the nodes
    ad_hoc_building = true;
    return createOrGetSubgraph(CInst, F);
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createOrGetSubgraph(const llvm::CallInst *CInst,
                                                const llvm::Function *F)
{
    PSNodesSeq cf = createCallToFunction(F);
    addNode(CInst, cf.first);

    if (ad_hoc_building) {
        Subgraph& subg = subgraphs_map[F];
        assert(subg.root != nullptr);

        addProgramStructure(F, subg);
        addInterproceduralOperands(F, subg, CInst);
    }

    // NOTE: we do not add return node into nodes_map, since this
    // is artificial node and does not correspond to any real node
    // FIXME: this breaks that we have a sequence in the graph

    return cf;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createUnknownCall(const llvm::CallInst *CInst)
{
    // This assertion must not hold if the call is wrapped
    // inside bitcast - it defaults to int, but is bitcased
    // to pointer
    //assert(CInst->getType()->isPointerTy());
    PSNode *call = PS.create(PSNodeType::CALL, nullptr);

    call->setPairedNode(call);

    // the only thing that the node will point at
    call->addPointsTo(PointerUnknown);

    addNode(CInst, call);

    return std::make_pair(call, call);
}

PSNode *LLVMPointerSubgraphBuilder::createMemTransfer(const llvm::IntrinsicInst *I)
{
    using namespace llvm;
    const Value *dest, *src;//, *lenVal;

    switch (I->getIntrinsicID()) {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            dest = I->getOperand(0);
            src = I->getOperand(1);
            //lenVal = I->getOperand(2);
            break;
        default:
            errs() << "ERR: unhandled mem transfer intrinsic" << *I << "\n";
            abort();
    }

    PSNode *destNode = getOperand(dest);
    PSNode *srcNode = getOperand(src);
    /* FIXME: compute correct value instead of UNKNOWN_OFFSET */
    PSNode *node = PS.create(PSNodeType::MEMCPY,
                              srcNode, destNode,
                              UNKNOWN_OFFSET,
                              UNKNOWN_OFFSET,
                              UNKNOWN_OFFSET);

    addNode(I, node);
    return node;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createVarArg(const llvm::IntrinsicInst *Inst)
{
    // just store all the pointers from vararg argument
    // to the memory given in vastart() on UNKNOWN_OFFSET.
    // It is the easiest thing we can do without any further
    // analysis

    // first we need to get the vararg argument phi
    const llvm::Function *F = Inst->getParent()->getParent();
    Subgraph& subg = subgraphs_map[F];
    PSNode *arg = subg.vararg;
    assert(F->isVarArg() && "vastart in a non-variadic function");
    assert(arg && "Don't have variadic argument in a variadic function");

    // vastart will be node that will keep the memory
    // with pointers, its argument is the alloca, that
    // alloca will keep pointer to vastart
    PSNode *vastart = PS.create(PSNodeType::ALLOC);

    // vastart has only one operand which is the struct
    // it uses for storing the va arguments. Strip it so that we'll
    // get the underlying alloca inst
    PSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    // the argument is usually an alloca, but it may be a load
    // in the case the code was transformed by -reg2mem
    assert((op->getType() == PSNodeType::ALLOC || op->getType() == PSNodeType::LOAD)
           && "Argument of vastart is invalid");
    // get node with the same pointer, but with UNKNOWN_OFFSET
    // FIXME: we're leaking it
    // make the memory in alloca point to our memory in vastart
    PSNode *ptr = PS.create(PSNodeType::GEP, op, UNKNOWN_OFFSET);
    PSNode *S1 = PS.create(PSNodeType::STORE, vastart, ptr);
    // and also make vastart point to the vararg args
    PSNode *S2 = PS.create(PSNodeType::STORE, arg, vastart);

    vastart->addSuccessor(ptr);
    ptr->addSuccessor(S1);
    S1->addSuccessor(S2);

    // set paired node to S2 for vararg, so that when adding structure,
    // we add the whole sequence (it adds from call-node to pair-node,
    // because of the old system where we did not store all sequences)
    // FIXME: fix this
    vastart->setPairedNode(S2);

    // FIXME: we're assuming that in a sequence in the nodes_map
    // is always the last node the 'real' node. In this case it is not true,
    // so add only the 'vastart', so that we have the mapping in nodes_map
    addNode(Inst, vastart);

    return PSNodesSeq(vastart, S2);
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createIntrinsic(const llvm::Instruction *Inst)
{
    using namespace llvm;
    PSNode *n;

    const IntrinsicInst *I = cast<IntrinsicInst>(Inst);
    if (isa<MemTransferInst>(I)) {
        n = createMemTransfer(I);
        return std::make_pair(n, n);
    } else if (isa<MemSetInst>(I)) {
        return createMemSet(I);
    }

    switch (I->getIntrinsicID()) {
        case Intrinsic::vastart:
            return createVarArg(I);
        case Intrinsic::stacksave:
            errs() << "WARNING: Saving stack may yield unsound results!: "
                   << *Inst << "\n";
            n = createAlloc(Inst);
            return std::make_pair(n, n);
        case Intrinsic::stackrestore:
            n = createLoad(Inst);
            return std::make_pair(n, n);
        default:
            errs() << *Inst << "\n";
            errs() << "Unhandled intrinsic ^^\n";
            abort();
    }
}

PSNode *
LLVMPointerSubgraphBuilder::createAsm(const llvm::Instruction *Inst)
{
    // we filter irrelevant calls in isRelevantCall()
    // and we don't have assembler there at all. If
    // we are here, then we got here because this
    // is undefined call that returns pointer.
    // In this case return an unknown pointer
    static bool warned = false;
    if (!warned) {
        llvm::errs() << "PTA: Inline assembly found, analysis  may be unsound\n";
        warned = true;
    }

    PSNode *n = PS.create(PSNodeType::CONSTANT, UNKNOWN_MEMORY, UNKNOWN_OFFSET);
    // it is call that returns pointer, so we'd like to have
    // a 'return' node that contains that pointer
    n->setPairedNode(n);
    addNode(Inst, n);

    return n;
}

PSNode * LLVMPointerSubgraphBuilder::createFree(const llvm::Instruction *Inst)
{
    PSNode *op1 = getOperand(Inst->getOperand(0));
    PSNode *node = PS.create(PSNodeType::FREE, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}


// create subgraph or add edges to already existing subgraph,
// return the CALL node (the first) and the RETURN node (the second),
// so that we can connect them into the PointerSubgraph
PSNodesSeq
LLVMPointerSubgraphBuilder::createCall(const llvm::Instruction *Inst)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();

    if (CInst->isInlineAsm()) {
        PSNode *n = createAsm(Inst);
        return std::make_pair(n, n);
    }

    const Function *func = dyn_cast<Function>(calledVal);
    if (func) {
        // is it a call to free? If so, create invalidate node
        // instead.
        if(invalidate_nodes && func->getName().equals("free")) {
            PSNode *n = createFree(Inst);
            return std::make_pair(n, n);
        }
        
        // is function undefined? If so it can be
        // intrinsic, memory allocation (malloc, calloc,...)
        // or just undefined function
        // NOTE: we first need to check whether the function
        // is undefined and after that if it is memory allocation,
        // because some programs may define function named
        // 'malloc' etc.
        if (func->size() == 0) {
            /// memory allocation (malloc, calloc, etc.)
            MemAllocationFuncs type = getMemAllocationFunc(func);
            if (type != MemAllocationFuncs::NONEMEM) {
                return createDynamicMemAlloc(CInst, type);
            } else if (func->isIntrinsic()) {
                return createIntrinsic(Inst);
            } else
                return createUnknownCall(CInst);
        } else {
            return createOrGetSubgraph(CInst, func);
        }
    } else {
        // function pointer call
        PSNode *op = getOperand(calledVal);
        PSNode *call_funcptr = PS.create(PSNodeType::CALL_FUNCPTR, op);
        PSNode *ret_call = PS.create(PSNodeType::CALL_RETURN, nullptr);

        ret_call->setPairedNode(call_funcptr);
        call_funcptr->setPairedNode(ret_call);

        call_funcptr->addSuccessor(ret_call);
        addNode(CInst, call_funcptr);

        return std::make_pair(call_funcptr, ret_call);
    }
}

PSNode *LLVMPointerSubgraphBuilder::createAlloc(const llvm::Instruction *Inst)
{
    PSNodeAlloc *node = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
    addNode(Inst, node);

    const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(Inst);
    if (AI)
        node->setSize(getAllocatedSize(AI, DL));

    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createStore(const llvm::Instruction *Inst)
{
    const llvm::Value *valOp = Inst->getOperand(0);

    PSNode *op1 = getOperand(valOp);
    PSNode *op2 = getOperand(Inst->getOperand(1));

    PSNode *node = PS.create(PSNodeType::STORE, op1, op2);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createLoad(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSNode *op1 = getOperand(op);
    PSNode *node = PS.create(PSNodeType::LOAD, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createGEP(const llvm::Instruction *Inst)
{
    using namespace llvm;

    const GetElementPtrInst *GEP = cast<GetElementPtrInst>(Inst);
    const Value *ptrOp = GEP->getPointerOperand();
    unsigned bitwidth = getPointerBitwidth(DL, ptrOp);
    APInt offset(bitwidth, 0);

    PSNode *node = nullptr;
    PSNode *op = getOperand(ptrOp);

    if (field_sensitivity > 0
        && GEP->accumulateConstantOffset(*DL, offset)) {
        // is offset in given bitwidth?
        if (offset.isIntN(bitwidth)) {
            // is 0 < offset < field_sensitivity ?
            uint64_t off = offset.getLimitedValue(field_sensitivity);
            if (off == 0 || off < field_sensitivity)
                node = PS.create(PSNodeType::GEP, op, offset.getZExtValue());
        } else
            errs() << "WARN: GEP offset greater than " << bitwidth << "-bit";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    // we didn't create the node with concrete offset,
    // in which case we are supposed to create a node
    // with UNKNOWN_OFFSET
    if (!node)
        node = PS.create(PSNodeType::GEP, op, UNKNOWN_OFFSET);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createSelect(const llvm::Instruction *Inst)
{
    // with ptrtoint/inttoptr it may not be a pointer
    // assert(Inst->getType()->isPointerTy() && "BUG: This select is not a pointer");

    // select <cond> <op1> <op2>
    PSNode *op1 = getOperand(Inst->getOperand(1));
    PSNode *op2 = getOperand(Inst->getOperand(2));

    // select works as a PHI in points-to analysis
    PSNode *node = PS.create(PSNodeType::PHI, op1, op2, nullptr);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createExtract(const llvm::Instruction *Inst)
{
    using namespace llvm;

    const ExtractValueInst *EI = cast<ExtractValueInst>(Inst);

    // extract <agg> <idx> {<idx>, ...}
    PSNode *op1 = getOperand(EI->getAggregateOperand());
    // FIXME: get the correct offset
    PSNode *G = PS.create(PSNodeType::GEP, op1, UNKNOWN_OFFSET);
    PSNode *L = PS.create(PSNodeType::LOAD, G);

    G->addSuccessor(L);

    PSNodesSeq ret = PSNodesSeq(G, L);
    addNode(Inst, ret);

    return ret;
}

PSNode *LLVMPointerSubgraphBuilder::createPHI(const llvm::Instruction *Inst)
{
    PSNode *node = PS.create(PSNodeType::PHI, nullptr);
    addNode(Inst, node);

    // NOTE: we didn't add operands to PHI node here, but after building
    // the whole function, because some blocks may not have been built
    // when we were creating the phi node

    assert(node);
    return node;
}

void LLVMPointerSubgraphBuilder::addPHIOperands(PSNode *node, const llvm::PHINode *PHI)
{
    for (int i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
        PSNode *op = tryGetOperand(PHI->getIncomingValue(i));
        if (op)
            node->addOperand(op);
    }
}

void LLVMPointerSubgraphBuilder::addPHIOperands(const llvm::Function &F)
{
    for (const llvm::BasicBlock& B : F) {
        for (const llvm::Instruction& I : B) {
            const llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(&I);
            if (PHI) {
                if (PSNode *node = getNode(PHI))
                    addPHIOperands(node, PHI);
            }
        }
    }
}

PSNode *LLVMPointerSubgraphBuilder::createCast(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSNode *op1 = getOperand(op);
    PSNode *node = PS.create(PSNodeType::CAST, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

// sometimes inttoptr is masked using & or | operators,
// so we need to support that. Anyway, that changes the pointer
// completely, so we just return unknown pointer
PSNode *LLVMPointerSubgraphBuilder::createUnknown(const llvm::Value *val)
{
    // nothing better we can do, these operations
    // completely change the value of pointer...

    // FIXME: or there's enough unknown offset? Check it out!
    PSNode *node = PS.create(PSNodeType::CONSTANT, UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    addNode(val, node);

    assert(node);
    return node;
}

// ptrToInt work just as a bitcast
PSNode *LLVMPointerSubgraphBuilder::createPtrToInt(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSNode *op1 = getOperand(op);
    // NOTE: we don't support arithmetic operations, so instead of
    // just casting the value do gep with unknown offset -
    // this way we cover any shift of the pointer due to arithmetic
    // operations
    // PSNode *node = PS.create(PSNodeType::CAST, op1);
    PSNode *node = PS.create(PSNodeType::GEP, op1, 0);
    addNode(Inst, node);

    // here we lost the type information,
    // so we must build all possible nodes that may affect
    // the pointer analysis
    transitivelyBuildUses(Inst);
    transitivelyBuildUses(Inst->getOperand(0));

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createIntToPtr(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSNode *op1;

    if (llvm::isa<llvm::Constant>(op)) {
        llvm::errs() << "PTA warning: IntToPtr with constant: "
                     << *Inst << "\n";
        // if this is inttoptr with constant, just make the pointer
        // unknown
        op1 = UNKNOWN_MEMORY;
    } else
        op1 = getOperand(op);

    PSNode *node = PS.create(PSNodeType::CAST, op1);
    addNode(Inst, node);

    // here we lost the type information,
    // so we must build all possible nodes that may affect
    // the pointer analysis
    transitivelyBuildUses(Inst);
    transitivelyBuildUses(Inst->getOperand(0));

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createAdd(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *node;
    PSNode *op;
    const Value *val = nullptr;
    uint64_t off = UNKNOWN_OFFSET;

    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
        val = Inst->getOperand(0);
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
        val = Inst->getOperand(1);
    } else {
        // the operands are both non-constant. Check if we
        // can get an operand as one of them and if not,
        // fall-back to unknown memory, because we
        // would need to track down both operads...
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    if (val)
        off = getConstantValue(val);

    node = PS.create(PSNodeType::GEP, op, off);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createArithmetic(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *node;
    PSNode *op;

    // we don't know if the operand is the first or
    // the other operand
    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
    } else if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(0));
    } else {
        // the operands are both non-constant. Check if we
        // can get an operand as one of them and if not,
        // fall-back to unknown memory, because we
        // would need to track down both operads...
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    // we don't know what the operation does,
    // so set unknown offset
    node = PS.create(PSNodeType::GEP, op, UNKNOWN_OFFSET);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createReturn(const llvm::Instruction *Inst)
{
    PSNode *op1 = nullptr;
    // is nullptr if this is 'ret void'
    llvm::Value *retVal = llvm::cast<llvm::ReturnInst>(Inst)->getReturnValue();

    // we create even void and non-pointer return nodes,
    // since these modify CFG (they won't bear any
    // points-to information though)
    // XXX is that needed?

    // DONT: if(retVal->getType()->isPointerTy())
    // we have ptrtoint which break the types...
    if (retVal) {
        if (llvm::isa<llvm::ConstantPointerNull>(retVal)
            || isConstantZero(retVal))
            op1 = NULLPTR;
        else if (typeCanBePointer(retVal->getType()) &&
                  (!isInvalid(retVal->stripPointerCasts()) ||
                   llvm::isa<llvm::ConstantExpr>(retVal)))
            op1 = getOperand(retVal);
    }

    assert((op1 || !retVal || !retVal->getType()->isPointerTy())
           && "Don't have operand for ReturnInst with pointer");

    PSNode *node = PS.create(PSNodeType::RETURN, op1, nullptr);
    addNode(Inst, node);

    return node;
}

void LLVMPointerSubgraphBuilder::transitivelyBuildUses(const llvm::Value *val)
{
    using namespace llvm;

    assert(!isa<ConstantInt>(val) && "Tried building uses of constant int");

    for (auto I = val->use_begin(), E = val->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const llvm::Value *use = *I;
#else
        const llvm::Value *use = I->getUser();
#endif

        if (isInvalid(use))
            continue;

        if (nodes_map.count(use) == 0) {
            PSNode *nd = buildNode(use);
            // we reached some point where we do not know
            // how to continue (the ptrtoint use-chains probably
            // got somewhere where we do not know how to handle
            // the instructions)
            if (nd == UNKNOWN_MEMORY)
                continue;

            if (const StoreInst *SI = dyn_cast<StoreInst>(use)) {
                // build the value, but only if it is a valid thing
                // - for example, we do not want to build constants
                if (!isInvalid(SI->getOperand(0)))
                    transitivelyBuildUses(SI->getOperand(0));
                // for StoreInst we need to get even uses
                // of the pointer, since we stored the value
                // into it (we want to have the loads from it)
                transitivelyBuildUses(SI->getOperand(1));
            } else if (const LoadInst *LI = dyn_cast<LoadInst>(use)) {
                transitivelyBuildUses(LI->getOperand(0));
            } else if (const CallInst *CI = dyn_cast<CallInst>(use)) {
                const Function *F = CI->getCalledFunction();
                if (F) {
                    // if this function is not built, build it
                    if (!F->empty()) {
                        Subgraph& subg = subgraphs_map[F];
                        if (!subg.root)
                            buildFunction(*F);
                        assert(subg.root && "Did not build the function");
                    }

                    // get the index of the use as an argument
                    int idx = 0;
                    for (int e = CI->getNumArgOperands(); idx < e; ++idx)
                        if (val == CI->getArgOperand(idx))
                            break;

                    // find the argument at the index
                    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, --idx) {
                        if (idx == 0) {
                            // if we have not built this argument yet,
                            // build it!
                            if (nodes_map.count(&*A) == 0) {
                                PSNode *nd = buildNode(&*A);
                                if (nd != UNKNOWN_MEMORY)
                                    transitivelyBuildUses(&*A);
                            }
                        }
                    }
                }
            }

            transitivelyBuildUses(use);
        }
    }
}

static bool isRelevantCall(const llvm::Instruction *Inst)
{
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that in PointerSubgraph
        return true;

    if (func->size() == 0) {
        if (getMemAllocationFunc(func) != MemAllocationFuncs::NONEMEM)
            // we need memory allocations
            return true;

        if (func->getName().equals("free"))
            // we need calls of free
            return true;

        if (func->isIntrinsic())
            return isRelevantIntrinsic(func);

        // returns pointer? We want that too - this is gonna be
        // an unknown pointer
        if (Inst->getType()->isPointerTy())
            return true;

        // XXX: what if undefined function takes as argument pointer
        // to memory with pointers? In that case to be really sound
        // we should make those pointers unknown. Another case is
        // what if the function returns a structure (is it possible in LLVM?)
        // It can return a structure containing a pointer - thus we should
        // make this pointer unknown

        // here we have: undefined function not returning a pointer
        // and not memory allocation: we don't need that
        return false;
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

PSNodesSeq
LLVMPointerSubgraphBuilder::buildInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;
    PSNode *node;

    switch(Inst.getOpcode()) {
        case Instruction::Alloca:
            node = createAlloc(&Inst);
            break;
        case Instruction::Store:
            node = createStore(&Inst);
            break;
        case Instruction::Load:
            node = createLoad(&Inst);
            break;
        case Instruction::GetElementPtr:
            node = createGEP(&Inst);
            break;
        case Instruction::ExtractValue:
            return createExtract(&Inst);
        case Instruction::Select:
            node = createSelect(&Inst);
            break;
        case Instruction::PHI:
            node = createPHI(&Inst);
            break;
        case Instruction::BitCast:
        case Instruction::SExt:
        case Instruction::ZExt:
            node = createCast(&Inst);
            break;
        case Instruction::PtrToInt:
            node = createPtrToInt(&Inst);
            break;
        case Instruction::IntToPtr:
            node = createIntToPtr(&Inst);
            break;
        case Instruction::Ret:
            node = createReturn(&Inst);
            break;
        case Instruction::Call:
            return createCall(&Inst);
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Trunc:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::Xor:
        case Instruction::FSub:
        case Instruction::FAdd:
        case Instruction::FDiv:
        case Instruction::FMul:
        case Instruction::UDiv:
        case Instruction::SDiv:
        case Instruction::URem:
        case Instruction::SRem:
        case Instruction::FRem:
        case Instruction::FPTrunc:
            // these instructions reinterpert the pointer,
            // nothing better we can do here (I think?)
            node = createUnknown(&Inst);
            break;
        case Instruction::Add:
            node = createAdd(&Inst);
            break;
        case Instruction::Sub:
        case Instruction::Mul:
            node = createArithmetic(&Inst);
            break;
        case Instruction::UIToFP:
        case Instruction::SIToFP:
            node = createCast(&Inst);
            break;
        case Instruction::FPToUI:
        case Instruction::FPToSI:
            if (typeCanBePointer(Inst.getType()))
                node = createCast(&Inst);
            else
                node = createUnknown(&Inst);
            break;
        default:
            llvm::errs() << Inst << "\n";
            assert(0 && "Unhandled instruction");
            node = createUnknown(&Inst);
    }

    return std::make_pair(node, node);
}

// is the instruction relevant to points-to analysis?
bool LLVMPointerSubgraphBuilder::isRelevantInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;

    switch(Inst.getOpcode()) {
        case Instruction::Store:
            // create only nodes that store pointer to another
            // pointer. We don't care about stores of non-pointers.
            // The only exception are stores to inttoptr nodes
            if (Inst.getOperand(0)->getType()->isPointerTy()
                // this will probably create the operand if we do not
                // have it, but we would create it later anyway
                || (tryGetOperand(Inst.getOperand(0)) != UNKNOWN_MEMORY))
                return true;
            else
                return false;
        case Instruction::ExtractValue:
            return Inst.getType()->isPointerTy();
        case Instruction::Load:
            // LLVM does optimizations like that this code
            // (it basically does ptrtoint using bitcast)
            //
            // %2 = GEP %a, 0, 0
            //
            // gets transformed to
            //
            // %1 = bitcast %a to *i32
            // %2 = load i32, i32* %1
            //
            // because that probably may be faster on 32-bit machines.
            // That completely breaks our relevancy criterions,
            // so we must use this hack (the same with store)
            if (tryGetOperand(Inst.getOperand(0)) != UNKNOWN_MEMORY)
                return true;
            /* fallthrough */
        case Instruction::Select:
        case Instruction::PHI:
            // here we don't care about intToPtr, because every such
            // value must be bitcasted first, and thus is a pointer
            if (Inst.getType()->isPointerTy())
                return true;
            else
                return false;
        case Instruction::Call:
            if (isRelevantCall(&Inst))
                return true;
            else
                return false;
        case Instruction::Alloca:
        case Instruction::GetElementPtr:
        case Instruction::BitCast:
        case Instruction::PtrToInt:
        case Instruction::IntToPtr:
        // we need to create every ret inst, because
        // it changes the flow of information
        case Instruction::Ret:
            return true;
        default:
            if (Inst.getType()->isPointerTy()) {
                llvm::errs() << "Unhandled relevant inst: " << Inst << "\n";
                abort();
            }

            return false;
    }

    assert(0 && "Not to be reached");
}

// create a formal argument
PSNode *LLVMPointerSubgraphBuilder::createArgument(const llvm::Argument *farg)
{
    using namespace llvm;

    PSNode *arg = PS.create(PSNodeType::PHI, nullptr);
    addNode(farg, arg);

    return arg;
}

static bool memsetIsZeroInitialization(const llvm::IntrinsicInst *I)
{
    return isConstantZero(I->getOperand(1));
}

// recursively find out if type contains a pointer type as a subtype
// (or if it is a pointer type itself)
static bool tyContainsPointer(const llvm::Type *Ty)
{
    if (Ty->isAggregateType()) {
        for (auto I = Ty->subtype_begin(), E = Ty->subtype_end();
             I != E; ++I) {
            if (tyContainsPointer(*I))
                return true;
        }
    } else
        return Ty->isPointerTy();

    return false;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createMemSet(const llvm::Instruction *Inst)
{
    PSNode *val;
    if (memsetIsZeroInitialization(llvm::cast<llvm::IntrinsicInst>(Inst)))
        val = NULLPTR;
    else
        // if the memset is not 0-initialized, it does some
        // garbage into the pointer
        val = UNKNOWN_MEMORY;

    PSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    // we need to make unknown offsets
    PSNode *G = PS.create(PSNodeType::GEP, op, UNKNOWN_OFFSET);
    PSNode *S = PS.create(PSNodeType::STORE, val, G);
    G->addSuccessor(S);

    PSNodesSeq ret = PSNodesSeq(G, S);
    addNode(Inst, ret);

    return ret;
}

void LLVMPointerSubgraphBuilder::checkMemSet(const llvm::Instruction *Inst)
{
    using namespace llvm;

    bool zeroed = memsetIsZeroInitialization(cast<IntrinsicInst>(Inst));
    if (!zeroed) {
        llvm::errs() << "WARNING: Non-0 memset: " << *Inst << "\n";
        return;
    }

    const Value *src = Inst->getOperand(0)->stripInBoundsOffsets();
    PSNode *op = getOperand(src);

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(src)) {
        // if there cannot be stored a pointer, we can bail out here
        // XXX: what if it is alloca of generic mem (e. g. [100 x i8])
        // and we then store there a pointer? Or zero it and load from it?
        // like:
        // char mem[100];
        // void *ptr = (void *) mem;
        // void *p = *ptr;
        if (tyContainsPointer(AI->getAllocatedType()))
            PSNodeAlloc::get(op)->setZeroInitialized();
    } else {
        // fallback: create a store that represents memset
        // the store will save null to ptr + UNKNOWN_OFFSET,
        // so we need to do:
        // G = GEP(op, UNKNOWN_OFFSET)
        // STORE(null, G)
        buildInstruction(*Inst);
    }
}

// return first and last nodes of the block
void LLVMPointerSubgraphBuilder::buildPointerSubgraphBlock(const llvm::BasicBlock& block)
{
    for (const llvm::Instruction& Inst : block) {
        if (!isRelevantInstruction(Inst)) {
            // check if it is a zeroing of memory,
            // if so, set the corresponding memory to zeroed
            if (llvm::isa<llvm::MemSetInst>(&Inst))
                checkMemSet(&Inst);

            continue;
        }

        // maybe this instruction was already created by getOperand()
        if (nodes_map.count(&Inst) != 0)
            continue;

        PSNodesSeq seq = buildInstruction(Inst);
        assert(seq.first && seq.second
               && "Didn't created the instruction properly");
    }
}

// build pointer state subgraph for given graph
// \return   root node of the graph
PSNode *LLVMPointerSubgraphBuilder::buildFunction(const llvm::Function& F)
{
    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    // XXX: do we need entry type?
    PSNode *root = PS.create(PSNodeType::ENTRY);
    PSNode *ret;

    if (invalidate_nodes) {
        ret = PS.create(PSNodeType::INVALIDATE_LOCALS, root);
    } else {
        ret = PS.create(PSNodeType::NOOP);
    }

    // if the function has variable arguments,
    // then create the node for it
    PSNode *vararg = nullptr;
    if (F.isVarArg())
        vararg = PS.create(PSNodeType::PHI, nullptr);

    // add record to built graphs here, so that subsequent call of this function
    // from buildPointerSubgraphBlock won't get stuck in infinite recursive call when
    // this function is recursive
    subgraphs_map[&F] = Subgraph(root, ret, vararg);

    // build the instructions from blocks
    for (const llvm::BasicBlock& block : F)
        buildPointerSubgraphBlock(block);

    // add operands to PHI nodes. It must be done after all blocks are
    // built, since the PHI gathers values from different blocks
    addPHIOperands(F);

    return root;
}

void LLVMPointerSubgraphBuilder::addProgramStructure()
{
    // form intraprocedural program structure (CFG edges)
    for (auto& it : subgraphs_map) {
        const llvm::Function *F = it.first;
        Subgraph& subg = it.second;

        // add the CFG edges
        addProgramStructure(F, subg);

        std::set<PSNode *> cont;
        getNodes(cont, subg.root, subg.ret, 0xdead);
        for (PSNode* n : cont) {
            n->setParent(subg.root);
        }

        // add the missing operands (to arguments and return nodes)
        addInterproceduralOperands(F, subg);
    }
}

void LLVMPointerSubgraphBuilder::addArgumentOperands(const llvm::CallInst *CI,
                                                     PSNode *arg, int idx)
{
    assert(idx < (int) CI->getNumArgOperands());
    PSNode *op = tryGetOperand(CI->getArgOperand(idx));
    if (op)
        arg->addOperand(op);
}

void LLVMPointerSubgraphBuilder::addArgumentOperands(const llvm::Function *F,
                                                     PSNode *arg, int idx)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addArgumentOperands(CI, arg, idx);
    }
}

void LLVMPointerSubgraphBuilder::addArgumentsOperands(const llvm::Function *F,
                                                      const llvm::CallInst *CI)
{
    int idx = 0;
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
        auto it = nodes_map.find(&*A);
        if (it == nodes_map.end())
            continue;

        PSNodesSeq& cur = it->second;
        assert(cur.first == cur.second);

        if (CI)
            // with func ptr call we know from which
            // call we should take the values
            addArgumentOperands(CI, cur.first, idx);
        else
            // with regular call just use all calls
            addArgumentOperands(F, cur.first, idx);
    }
}

void LLVMPointerSubgraphBuilder::addVariadicArgumentOperands(const llvm::Function *F,
                                                             const llvm::CallInst *CI,
                                                             PSNode *arg)
{
    for (unsigned idx = F->arg_size() - 1; idx < CI->getNumArgOperands(); ++idx)
        addArgumentOperands(CI, arg, idx);
}

void LLVMPointerSubgraphBuilder::addVariadicArgumentOperands(const llvm::Function *F,
                                                             PSNode *arg)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addVariadicArgumentOperands(F, CI, arg);
        // if this is funcptr, we handle it in the other
        // version of addVariadicArgumentOperands
    }
}

void LLVMPointerSubgraphBuilder::addReturnNodeOperands(const llvm::Function *F,
                                                       PSNode *ret,
                                                       const llvm::CallInst *CI)
{
    using namespace llvm;

    for (PSNode *r : ret->getPredecessors()) {
        // return node is like a PHI node,
        // we must add the operands too.
        // But we're interested only in the nodes that return some value
        // from subprocedure, not for all nodes that have no successor
        if (r->getType() == PSNodeType::RETURN) {
            if (CI)
                addReturnNodeOperand(CI, r);
            else
                addReturnNodeOperand(F, r);
        }
    }
}

void LLVMPointerSubgraphBuilder::addReturnNodeOperand(const llvm::CallInst *CI, PSNode *op)
{
    PSNode *callNode = getNode(CI);
    // since we're building the graph from main and only where we can reach it,
    // we may not have all call-sites of a function
    if (!callNode)
        return;

    PSNode *returnNode = callNode->getPairedNode();
    // the function must be defined, since we have the return node,
    // so there must be associated the return node
    assert(returnNode);

    returnNode->addOperand(op);
}


void LLVMPointerSubgraphBuilder::addReturnNodeOperand(const llvm::Function *F, PSNode *op)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        // get every call and its assocciated return and add the operand
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addReturnNodeOperand(CI, op);
    }
}

void LLVMPointerSubgraphBuilder::addInterproceduralOperands(const llvm::Function *F,
                                                            Subgraph& subg,
                                                            const llvm::CallInst *CI)
{
    // add operands to arguments' PHI nodes
    addArgumentsOperands(F, CI);

    if (F->isVarArg()) {
        assert(subg.vararg);
        if (CI)
            // funcptr call
            addVariadicArgumentOperands(F, CI, subg.vararg);
        else
            addVariadicArgumentOperands(F, subg.vararg);
    }

    addReturnNodeOperands(F, subg.ret, CI);
}


PointerSubgraph *LLVMPointerSubgraphBuilder::buildLLVMPointerSubgraph()
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    PSNodesSeq glob = buildGlobals();

    // now we can build rest of the graph
    PSNode *root = buildFunction(*F);

    // fill in the CFG edges
    addProgramStructure();

    // do we have any globals at all? If so, insert them at the begining
    // of the graph
    // FIXME: we do not need to process them later,
    // should we do it somehow differently?
    // something like 'static nodes' in PointerSubgraph...
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);
        root = glob.first;
    }

    PS.setRoot(root);
    return &PS;
}

} // namespace pta
} // namespace analysis
} // namespace dg
