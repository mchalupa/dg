#include <cassert>

#include <llvm/Config/llvm-config.h>
#if (LLVM_VERSION_MINOR < 5)
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

#include "analysis/PointsTo/PSS.h"
#include "PSS.h"

#ifdef DEBUG_ENABLED
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#endif

namespace dg {
namespace analysis {
namespace pss {

/* keep it for debugging */
#if 0
static std::string
getInstName(const llvm::Value *val)
{
    using namespace llvm;

    std::ostringstream ostr;
    raw_os_ostream ro(ostr);

    assert(val);
    if (const Function *F = dyn_cast<Function>(val))
        ro << F->getName().data();
    else
        ro << *val;

    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

const char *__get_name(const llvm::Value *val, const char *prefix)
{
    static std::string buf;
    buf.reserve(255);
    buf.clear();

    std::string nm = getInstName(val);
    if (prefix)
        buf.append(prefix);

    buf.append(nm);

    return buf.c_str();
}

{
    const char *name = __get_name(val, prefix);
}

{
    if (prefix) {
        std::string nm;
        nm.append(prefix);
        nm.append(name);
    } else
}
#endif

enum MemAllocationFuncs {
    NONEMEM = 0,
    MALLOC,
    CALLOC,
    ALLOCA,
    REALLOC,
};

static int getMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return NONEMEM;

    const char *name = func->getName().data();
    if (strcmp(name, "malloc") == 0)
        return MALLOC;
    else if (strcmp(name, "calloc") == 0)
        return CALLOC;
    else if (strcmp(name, "alloca") == 0)
        return ALLOCA;
    else if (strcmp(name, "realloc") == 0)
        return REALLOC;

    return NONEMEM;
}

static inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

static uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return 0;

    return DL->getTypeAllocSize(Ty);
}

Pointer LLVMPSSBuilder::handleConstantBitCast(const llvm::BitCastInst *BC)
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
    PSSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant BitCast with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPSSBuilder::handleConstantGep(const llvm::GetElementPtrInst *GEP)
{
    using namespace llvm;

    const Value *op = GEP->getPointerOperand();
    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    // get operand PSSNode (this may result in recursive call,
    // if this gep is recursively defined)
    PSSNode *opNode = getOperand(op);
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

Pointer LLVMPSSBuilder::getConstantExprPointer(const llvm::ConstantExpr *CE)
{
    using namespace llvm;

    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
    const Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();

    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
        pointer = handleConstantGep(GEP);
    } else if (const BitCastInst *BC = dyn_cast<BitCastInst>(Inst)) {
        pointer = handleConstantBitCast(BC);
    } else if (isa<IntToPtrInst>(Inst)) {
        // FIXME: we can do more!
        pointer = PointerUnknown;
    } else {
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

    delete Inst;
    return pointer;
}

PSSNode *LLVMPSSBuilder::createConstantExpr(const llvm::ConstantExpr *CE)
{
    Pointer ptr = getConstantExprPointer(CE);
    PSSNode *node = new PSSNode(pss::CONSTANT, ptr.target, ptr.offset);

    addNode(CE, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::getConstant(const llvm::Value *val)
{
    if (llvm::isa<llvm::ConstantPointerNull>(val)) {
        return NULLPTR;
    } else if (llvm::isa<llvm::UndefValue>(val)) {
        return UNKNOWN_MEMORY;
    } else if (const llvm::ConstantExpr *CE
                    = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
        return createConstantExpr(CE);
    } else if (llvm::isa<llvm::Function>(val)) {
        PSSNode *ret = new PSSNode(FUNCTION);
        addNode(val, ret);

        return ret;
    } else
        return nullptr;
}

// try get operand, return null if no such value has been constructed
PSSNode *LLVMPSSBuilder::tryGetOperand(const llvm::Value *val)
{
    auto it = nodes_map.find(val);
    PSSNode *op = nullptr;

    if (it != nodes_map.end())
        op = it->second;

    // if we don't have the operand, then it is a ConstantExpr
    // or some operand of intToPtr instruction (or related to that)
    if (!op) {
        if (llvm::isa<llvm::Constant>(val)) {
            op = getConstant(val);
            if (!op) {
                // unknown constant
                llvm::errs() << "ERR: unknown constant: " << *val << "\n";
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
    if (op->getType() == pss::CALL
        || op->getType() == pss::CALL_FUNCPTR) {
        op = op->getPairedNode();
    }

    return op;
}

PSSNode *LLVMPSSBuilder::getOperand(const llvm::Value *val)
{
    PSSNode *op = tryGetOperand(val);
    if (!op) {
        const llvm::Instruction *Inst
            = llvm::dyn_cast<llvm::Instruction>(val);

        if (Inst && !isRelevantInstruction(*Inst)) {
            // Create irrelevant operand if we don't have it.
            // We will place it later
            op = createIrrelevantInst(Inst, false);
        } else if (const llvm::Argument *A
                    = llvm::dyn_cast<llvm::Argument>(val)) {
            op = createIrrelevantArgument(A);
        } else {
            llvm::errs() << "Did not find an operand: " << *val << "\n";
            abort();
        }
    }

    return op;
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

static PSSNode *createDynamicAlloc(const llvm::CallInst *CInst, int type)
{
    using namespace llvm;

    const Value *op;
    uint64_t size = 0, size2 = 0;
    PSSNode *node = new PSSNode(pss::DYN_ALLOC);

    switch (type) {
        case MALLOC:
            node->setIsHeap();
        case ALLOCA:
            op = CInst->getOperand(0);
            break;
        case CALLOC:
            node->setIsHeap();
            node->setZeroInitialized();
            op = CInst->getOperand(1);
            break;
        default:
            errs() << *CInst << "\n";
            assert(0 && "unknown memory allocation type");
    };

    // infer allocated size
    size = getConstantValue(op);
    if (size != 0 && type == CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node->setSize(size);
    return node;
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createRealloc(const llvm::CallInst *CInst)
{
    using namespace llvm;

    // we create new allocation node and memcpy old pointers there
    PSSNode *orig_mem = getOperand(CInst->getOperand(0)->stripInBoundsOffsets());
    PSSNode *reall = new PSSNode(pss::DYN_ALLOC);
    // copy everything that is in orig_mem to reall
    PSSNode *mcp = new PSSNode(pss::MEMCPY, orig_mem, reall, 0, UNKNOWN_OFFSET);

    reall->setIsHeap();
    reall->setSize(getConstantValue(CInst->getOperand(1)));
    if (orig_mem->isZeroInitialized())
        reall->setZeroInitialized();

    reall->addSuccessor(mcp);
    addNode(CInst, reall);

    return std::make_pair(reall, mcp);
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createDynamicMemAlloc(const llvm::CallInst *CInst, int type)
{
    assert(type != NONEMEM
            && "BUG: creating dyn. memory node for NONMEM");

    if (type == REALLOC) {
        return createRealloc(CInst);
    } else {
        PSSNode *node = createDynamicAlloc(CInst, type);
        addNode(CInst, node);

        // we return (node, node), so that the parent function
        // will seamlessly connect this node into the graph
        return std::make_pair(node, node);
    }
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createCallToFunction(const llvm::CallInst *CInst,
                                     const llvm::Function *F)
{
    PSSNode *callNode, *returnNode;

    // the operands to the return node (which works as a phi node)
    // are going to be added when the subgraph is built
    returnNode = new PSSNode(pss::CALL_RETURN, nullptr);
    callNode = new PSSNode(pss::CALL, nullptr);

    returnNode->setPairedNode(callNode);
    callNode->setPairedNode(returnNode);

    // reuse built subgraphs if available
    Subgraph subg = subgraphs_map[F];
    if (!subg.root) {
        // create new subgraph
        buildLLVMPSS(*F);
        // FIXME: don't find it again, return it from buildLLVMPSS
        // this is redundant
        subg = subgraphs_map[F];
    }

    assert(subg.root && subg.ret);

    // add an edge from last argument to root of the subgraph
    // and from the subprocedure return node (which is one - unified
    // for all return nodes) to return from the call
    callNode->addSuccessor(subg.root);
    subg.ret->addSuccessor(returnNode);

    // add pointers to the arguments PHI nodes
    int idx = 0;
    PSSNode *arg = subg.args.first;
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
        if (A->getType()->isPointerTy()) {
            assert(arg && "BUG: do not have argument");

            PSSNode *op = getOperand(CInst->getArgOperand(idx));
            arg->addOperand(op);

            // shift in arguments
            assert(arg->successorsNum() <= 1);
            if (arg->successorsNum() == 1)
                arg = arg->getSingleSuccessor();
        }
    }

    // is the function variadic? arg now contains the last argument node,
    // which is the variadic one and idx should be index of the first
    // value passed as variadic. So go through the rest of callinst
    // arguments and if some of them is pointer, add it as an operand
    // to the phi node
    if (F->isVarArg()) {
        assert(arg);
        for (unsigned int i = idx; i < CInst->getNumArgOperands(); ++i) {
            const llvm::Value *llvmOp = CInst->getArgOperand(i);
            if (llvmOp->getType()->isPointerTy()) {
                PSSNode *op = getOperand(llvmOp);
                arg->addOperand(op);
            }
        }
    }

    // handle value returned from the function if it is a pointer
    // DONT: if (CInst->getType()->isPointerTy()) {
    // we need to handle the return values even when it is not
    // a pointer as we have ptrtoint and inttoptr

    // return node is like a PHI node
    for (PSSNode *r : subg.ret->getPredecessors())
        // we're interested only in the nodes that return some value
        // from subprocedure, not for all nodes that have no successor
        if (r->getType() == pss::RETURN)
            returnNode->addOperand(r);

    return std::make_pair(callNode, returnNode);
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createOrGetSubgraph(const llvm::CallInst *CInst,
                                    const llvm::Function *F)
{
    std::pair<PSSNode *, PSSNode *> cf = createCallToFunction(CInst, F);
    addNode(CInst, cf.first);

    // NOTE: we do not add return node into nodes_map, since this
    // is artificial node and does not correspond to any real node

    return cf;
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createUnknownCall(const llvm::CallInst *CInst)
{
    // This assertion must not hold if the call is wrapped
    // inside bitcast - it defaults to int, but is bitcased
    // to pointer
    //assert(CInst->getType()->isPointerTy());
    PSSNode *call = new PSSNode(pss::CALL, nullptr);

    call->setPairedNode(call);

    // the only thing that the node will point at
    call->addPointsTo(PointerUnknown);

    addNode(CInst, call);

    return std::make_pair(call, call);
}

PSSNode *LLVMPSSBuilder::createMemTransfer(const llvm::IntrinsicInst *I)
{
    using namespace llvm;
    const Value *dest, *src, *lenVal;

    switch (I->getIntrinsicID()) {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            dest = I->getOperand(0);
            src = I->getOperand(1);
            lenVal = I->getOperand(2);
            break;
        default:
            errs() << "ERR: unhandled mem transfer intrinsic" << *I << "\n";
            abort();
    }

    PSSNode *destNode = getOperand(dest);
    PSSNode *srcNode = getOperand(src);
    /* FIXME: compute correct value instead of UNKNOWN_OFFSET */
    PSSNode *node = new PSSNode(MEMCPY, srcNode, destNode,
                                UNKNOWN_OFFSET, UNKNOWN_OFFSET);

    addNode(I, node);
    return node;
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createVarArg(const llvm::IntrinsicInst *Inst)
{
    // just store all the pointers from vararg argument
    // to the memory given in vastart() on UNKNOWN_OFFSET.
    // It is the easiest thing we can do without any further
    // analysis

    // first we need to get the vararg argument phi
    const llvm::Function *F = Inst->getParent()->getParent();
    Subgraph& subg = subgraphs_map[F];
    PSSNode *arg = subg.args.second;
    assert(F->isVarArg() && "vastart in non-variadic function");
    assert(arg && "Don't have variadic argument in variadic function");

    // vastart will be node that will keep the memory
    // with pointers, its argument is the alloca, that
    // alloca will keep pointer to vastart
    PSSNode *vastart = new PSSNode(pss::ALLOC);

    // vastart has only one operand which is the struct
    // it uses for storing the va arguments. Strip it so that we'll
    // get the underlying alloca inst
    PSSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    assert(op->getType() == pss::ALLOC
           && "Argument of vastart is not an alloca");
    // get node with the same pointer, but with UNKNOWN_OFFSET
    // FIXME: we're leaking it
    // make the memory in alloca point to our memory in vastart
    PSSNode *ptr = new PSSNode(pss::CONSTANT, op, UNKNOWN_OFFSET);
    PSSNode *S1 = new PSSNode(pss::STORE, vastart, ptr);
    // and also make vastart point to the vararg args
    PSSNode *S2 = new PSSNode(pss::STORE, arg, vastart);

    addNode(Inst, vastart);

    vastart->addSuccessor(S1);
    S1->addSuccessor(S2);

    return std::make_pair(vastart, S2);
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createIntrinsic(const llvm::Instruction *Inst)
{
    using namespace llvm;
    PSSNode *n;

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
            assert(0 && "Unhandled intrinsic");
    }
}

PSSNode *
LLVMPSSBuilder::createAsm(const llvm::Instruction *Inst)
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

    PSSNode *n = new PSSNode(pss::CONSTANT, UNKNOWN_MEMORY, UNKNOWN_OFFSET);
    // it is call that returns pointer, so we'd like to have
    // a 'return' node that contains that pointer
    n->setPairedNode(n);
    addNode(Inst, n);

    return n;
}

// create subgraph or add edges to already existing subgraph,
// return the CALL node (the first) and the RETURN node (the second),
// so that we can connect them into the PSS
std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createCall(const llvm::Instruction *Inst)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();

    if (CInst->isInlineAsm()) {
        PSSNode *n = createAsm(Inst);
        return std::make_pair(n, n);
    }

    const Function *func = dyn_cast<Function>(calledVal);
    if (func) {
        // is function undefined? If so it can be
        // intrinsic, memory allocation (malloc, calloc,...)
        // or just undefined function
        // NOTE: we firt need to check whether the function
        // is undefined and after that if it is memory allocation,
        // because some programs may define function named
        // 'malloc' etc.
        if (func->size() == 0) {
            /// memory allocation (malloc, calloc, etc.)
            if (int type = getMemAllocationFunc(func)) {
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
        PSSNode *op = getOperand(calledVal);
        PSSNode *call_funcptr = new PSSNode(pss::CALL_FUNCPTR, op);
        PSSNode *ret_call = new PSSNode(RETURN, nullptr);

        ret_call->setPairedNode(call_funcptr);
        call_funcptr->setPairedNode(ret_call);

        call_funcptr->addSuccessor(ret_call);
        addNode(CInst, call_funcptr);

        return std::make_pair(call_funcptr, ret_call);
    }
}

PSSNode *LLVMPSSBuilder::createAlloc(const llvm::Instruction *Inst)
{
    PSSNode *node = new PSSNode(pss::ALLOC);
    addNode(Inst, node);

    const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(Inst);
    if (AI)
        node->setSize(getAllocatedSize(AI->getAllocatedType(), DL));

    return node;
}

PSSNode *LLVMPSSBuilder::createStore(const llvm::Instruction *Inst)
{
    const llvm::Value *valOp = Inst->getOperand(0);

    PSSNode *op1 = getOperand(valOp);
    PSSNode *op2 = getOperand(Inst->getOperand(1));

    PSSNode *node = new PSSNode(pss::STORE, op1, op2);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createLoad(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSSNode *op1 = getOperand(op);
    PSSNode *node = new PSSNode(pss::LOAD, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createGEP(const llvm::Instruction *Inst)
{
    using namespace llvm;

    const GetElementPtrInst *GEP = cast<GetElementPtrInst>(Inst);
    const Value *ptrOp = GEP->getPointerOperand();
    unsigned bitwidth = getPointerBitwidth(DL, ptrOp);
    APInt offset(bitwidth, 0);

    PSSNode *node = nullptr;
    PSSNode *op = getOperand(ptrOp);

    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth))
            node = new PSSNode(pss::GEP, op, offset.getZExtValue());
        else
            errs() << "WARN: GEP offset greater than " << bitwidth << "-bit";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    if (!node)
        node = new PSSNode(pss::GEP, op, UNKNOWN_OFFSET);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createSelect(const llvm::Instruction *Inst)
{
    // the value needs to be a pointer - we call this function only under
    // this condition
    assert(Inst->getType()->isPointerTy() && "BUG: This select is not a pointer");

    // select <cond> <op1> <op2>
    PSSNode *op1 = getOperand(Inst->getOperand(1));
    PSSNode *op2 = getOperand(Inst->getOperand(2));

    // select works as a PHI in points-to analysis
    PSSNode *node = new PSSNode(pss::PHI, op1, op2, nullptr);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createPHI(const llvm::Instruction *Inst)
{
    // we need a pointer
    assert(Inst->getType()->isPointerTy() && "BUG: This PHI is not a pointer");

    PSSNode *node = new PSSNode(pss::PHI, nullptr);
    addNode(Inst, node);

    // NOTE: we didn't add operands to PHI node here, but after building
    // the whole function, because some blocks may not have been built
    // when we were creating the phi node

    assert(node);
    return node;
}

void LLVMPSSBuilder::addPHIOperands(PSSNode *node, const llvm::PHINode *PHI)
{
    assert(PHI->getType()->isPointerTy() && "BUG: This PHI is not a pointer");

    for (int i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
        PSSNode *op = getOperand(PHI->getIncomingValue(i));
        node->addOperand(op);
    }
}

void LLVMPSSBuilder::addPHIOperands(const llvm::Function &F)
{
    for (const llvm::BasicBlock& B : F) {
        for (const llvm::Instruction& I : B) {
            if (!I.getType()->isPointerTy())
                continue;

            const llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(&I);
            if (PHI)
                addPHIOperands(getNode(PHI), PHI);
        }
    }
}

PSSNode *LLVMPSSBuilder::createCast(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1 = getOperand(op);
    PSSNode *node = new PSSNode(pss::CAST, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

// sometimes inttoptr is masked using & or | operators,
// so we need to support that. Anyway, that changes the pointer
// completely, so we just return unknown pointer
PSSNode *LLVMPSSBuilder::createUnknown(const llvm::Instruction *Inst)
{
    // nothing better we can do, these operations
    // completely change the value of pointer...

    // FIXME: or there's enough unknown offset? Check it out!
    PSSNode *node = new PSSNode(pss::CONSTANT, UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    addNode(Inst, node);

    assert(node);
    return node;
}

// ptrToInt work just as a bitcast
PSSNode *LLVMPSSBuilder::createPtrToInt(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSSNode *op1 = getOperand(op);
    // NOTE: we don't support arithmetic operations, so instead of
    // just casting the value do gep with unknown offset -
    // this way we cover any shift of the pointer due to arithmetic
    // operations
    // PSSNode *node = new PSSNode(pss::CAST, op1);
    PSSNode *node = new PSSNode(pss::GEP, op1, 0);
    addNode(Inst, node);

    // we need to build uses for this instruction, but we need to
    // do it later, when we have all blocks build
    buildUses.insert(Inst);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createIntToPtr(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1;

    if (llvm::isa<llvm::Constant>(op)) {
        llvm::errs() << "PTA warning: IntToPtr with constant: "
                     << *Inst << "\n";
        // if this is inttoptr with constant, just make the pointer
        // unknown
        op1 = UNKNOWN_MEMORY;
    } else
        op1 = getOperand(op);

    PSSNode *node = new PSSNode(pss::CAST, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createAdd(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSSNode *node;
    const Value *val = Inst->getOperand(1);
    PSSNode *op = tryGetOperand(Inst->getOperand(0));

    // add val1, val2
    // we don't know if val1 is the pointer or the val2
    // is the pointer, so we must check both
    if (!op) {
        op = tryGetOperand(Inst->getOperand(1));
        val = Inst->getOperand(0);
    }


    assert(op && "Don't have operand for add");

    uint64_t off = getConstantValue(val);
    node = new PSSNode(pss::GEP, op, off);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createArithmetic(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSSNode *node;
    PSSNode *op = tryGetOperand(Inst->getOperand(0));

    // this is binary operation, but we don't
    // know which operand is a pointer, so we must check both
    if (!op)
        op = tryGetOperand(Inst->getOperand(1));

    assert(op && "Don't have operand for binary op");
    // we don't know what the operation does,
    // so set unknown offset
    node = new PSSNode(pss::GEP, op, UNKNOWN_OFFSET);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createReturn(const llvm::Instruction *Inst)
{
    PSSNode *op1 = nullptr;
    // is nullptr if this is 'ret void'
    llvm::Value *retVal = llvm::cast<llvm::ReturnInst>(Inst)->getReturnValue();

    // we create even void and non-pointer return nodes,
    // since these modify CFG (they won't bear any
    // points-to information though)
    // XXX is that needed?

    // DONT: if(retVal->getType()->isPointerTy())
    // we have ptrtoint which break the types...
    if (retVal && nodes_map.count(retVal))
        op1 = getOperand(retVal);

    assert((op1 || !retVal || !retVal->getType()->isPointerTy())
           && "Don't have operand for ReturnInst with pointer");

    PSSNode *node = new PSSNode(pss::RETURN, op1, nullptr);
    addNode(Inst, node);

    return node;
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
        // function pointer call - we need that in PSS
        return true;

    if (func->size() == 0) {
        if (getMemAllocationFunc(func))
            // we need memory allocations
            return true;

        if (func->isIntrinsic()) {
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

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::buildInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;
    PSSNode *node;

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
            // these instructions reinterpert the pointer,
            // nothing better we can do here (I think?)
            node = createUnknown(&Inst);
            break;
        case Instruction::Add:
            node = createAdd(&Inst);
            break;
        case Instruction::Sub:
            llvm::errs() << "FIXME: handle Sub with constants as GEP\n";
        case Instruction::Mul:
        case Instruction::SDiv:
            node = createArithmetic(&Inst);
            break;
        default:
            llvm::errs() << Inst << "\n";
            assert(0 && "Unhandled instruction");
    }

    return std::make_pair(node, node);
}

// is the instruction relevant to points-to analysis?
bool LLVMPSSBuilder::isRelevantInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;

    switch(Inst.getOpcode()) {
        case Instruction::Store:
            // create only nodes that store pointer to another
            // pointer. We don't care about stores of non-pointers.
            // The only exception are stores to inttoptr nodes
            if (Inst.getOperand(0)->getType()->isPointerTy())
                return true;
            else
                return false;
        case Instruction::Load:
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
            return false;
    }

    assert(0 && "Not to be reached");
}

// this method creates a node no matter if it is pointer-related
// instruction. Then it inserts the node (sequence of nodes) into
// the PSS. This is needed due to arguments of intToPtr instructions,
// because these are not of pointer-type, therefore are not built
// in buildPSSBlock
PSSNode *LLVMPSSBuilder::createIrrelevantInst(const llvm::Value *val,
                                              bool build_uses)

{
    using namespace llvm;
    const llvm::Instruction *Inst = cast<Instruction>(val);

    // this instruction must be irreleventa, otherwise
    // we would build it in buildPSSBlock
    assert(!isRelevantInstruction(*Inst));

    // build the node for the instruction
    std::pair<PSSNode *, PSSNode *> seq = buildInstruction(*Inst);

    //errs() << "WARN: Built irrelevant inst: " << *val << "\n";

    // insert it to unplacedInstructions, we will put it
    // into the PSS later when we have all basic blocks
    // created
    unplacedInstructions.insert(seq);

    // add node to the map. We suppose that only the
    // last node is 'real' i. e. has corresponding llvm value
    addNode(val, seq.second);

    // we should build recurently uses of this instruction
    // that are also "irrelevant"?
    if (build_uses)
        createIrrelevantUses(val);

    return seq.first;
}

// create a formal argument
PSSNode *LLVMPSSBuilder::createIrrelevantArgument(const llvm::Argument *farg)
{
    using namespace llvm;

    PSSNode *arg = new PSSNode(pss::PHI, nullptr);
    addNode(farg, arg);
    unplacedInstructions.insert(std::make_pair(arg, arg));
    llvm::errs() << "WARN: built irrelevant arg: " << *farg << "\n";

    return arg;
}

void LLVMPSSBuilder::createIrrelevantUses(const llvm::Value *val)
{
    using namespace llvm;

    // NOTE: go backward the uses list, so that we first discover
    // the close uses and then the uses that are further in the program
    // I haven't find out how to use something like reverse iterator,
    // so we hack it here with vector...
    std::vector<const Value *> uses;
    for (auto I = val->use_begin(), E = val->use_end(); I != E; ++I) {
#if (LLVM_VERSION_MINOR < 5)
        const llvm::Value *use = *I;
#else
        const llvm::Value *use = I->getUser();
#endif
        // these uses we don't want to build
        if (isa<ICmpInst>(use))
            continue;

        uses.push_back(use);
    }

    // go backward the uses we gathered
    for (int i = uses.size() - 1; i >= 0; --i) {
        const Value *use = uses[i];
        const Instruction *Inst = dyn_cast<Instruction>(use);

        // create the irrelevant instruction if we don't have
        // it created already
        if (Inst && nodes_map.count(use) == 0) {
            if (!isRelevantInstruction(*Inst)) {
                createIrrelevantInst(use, true /* recursive */);

                if (isa<StoreInst>(use))
                    // for StoreInst we need to create even uses
                    // of the pointer, since we stored the value
                    // into it (we want to have the loads from it)
                    createIrrelevantUses(Inst->getOperand(1));
            }

            if (const CallInst *CI = dyn_cast<CallInst>(use)) {
                // if the use is CallInst, then we use the value
                // as an argument - we need to build new argument
                // and put it into the procedure later

                const Function *F = CI->getCalledFunction();
                assert(F && "ptrtoint via function pointer call not implemented");

                // find the formal argument of function into which we pass
                // the value
                const llvm::Value *farg = nullptr;
                int idx = 0;
                for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
                    if (CI->getArgOperand(idx) == val) {
                        farg = &*A;
                        break;
                    }
                }

                // did not found?
                if (!farg) {
                    // FIXME: vararg functions.
                    // and what about calll of vararg via pointer?
                    // or just call via pointer? Than we cannot create it...
                    // maan, the ptrtoint will kill me...
                    assert(0 && "Did not find the use of val. "
                                "If this is a vararg, this is not implemented yet");
                }

                // create the argument now if we haven't created it due to some
                // use as operand in an instruction earlier
                PSSNode *arg = nodes_map[farg];
                if (!arg) {
                    arg = createIrrelevantArgument(llvm::cast<llvm::Argument>(farg));
                }

                // add the PHI operands
                arg->addOperand(getOperand(val));

                // and we also need to build the instructions that use the formal
                // parameter
                createIrrelevantUses(farg);
            }
        }
    }
}
void LLVMPSSBuilder::buildUnbuiltUses(void)
{
    for (const llvm::Value *use : buildUses)
        createIrrelevantUses(use);

    buildUses.clear();
}

void LLVMPSSBuilder::addUnplacedInstructions(void)
{
    // Insert the irrelevant instructions into the tree.
    // Find the block that the instruction belongs and insert it
    // into it onto the right place
    for (std::pair<PSSNode *, PSSNode *> seq : unplacedInstructions) {
        // the last element contains the representant
        PSSNode *node = seq.second;
        const llvm::Value *val = node->getUserData<llvm::Value>();

        if (const llvm::Argument *arg = llvm::dyn_cast<llvm::Argument>(val)) {
            // we created an argument - put it in to the end
            // of arguments of its function
            Subgraph& subg = subgraphs_map[arg->getParent()];
            assert(subg.root && "Don't have subgraph");
            assert(seq.first == seq.second);

            // we do not have any arguments?
            if (!subg.args.second) {
                assert(!subg.args.first && "Have one but not the other argument");

                node->insertAfter(subg.root);
                // update the arguments, we want consistent information
                subg.args.first = subg.args.second = node;
            } else {
                // we have, so insert it at the end
                node->insertAfter(subg.args.second);
                // update the arguments
                subg.args.second = node;
            }

            continue;
        }

        const llvm::Instruction *Inst = llvm::cast<llvm::Instruction>(val);
        const llvm::BasicBlock *llvmBlk = Inst->getParent();

        // we must already created the block
        assert(built_blocks.count(llvmBlk) == 1
               && "BUG: we should have this block created");
        std::pair<PSSNode *, PSSNode *>& blk = built_blocks[llvmBlk];
        if (blk.first) {
            auto I = llvmBlk->begin();
            // shift to our instruction
            while (&*I != val)
                ++I;

            // now shift to the successor of our instruction,
            // so that we won't match our instruction later
            // (it is in nodes_map already)
            ++I;

            // OK, we found our instruction in block,
            // now find first instruction that we created in PSS
            auto end = nodes_map.end();
            for (auto E = llvmBlk->end(); I != E; ++I) {
                auto cur = nodes_map.find(&*I);
                if (cur == end)
                    // don't have it in the map
                    continue;

                // found inst that we have created?
                // check if it is already placed
                // (we don't want to place this node
                // according another unplaced node)
                // and if so, go with that
                if (cur->second->predecessorsNum() != 0
                    || cur->second->successorsNum() != 0)
                    break;
            }

            // now we have in I iterator to the LLVM value that
            // is in PSS as the first after our value
            if (I == llvmBlk->end()) {
                // did we get at the end of the block?
                blk.second->addSuccessor(seq.first);
                blk.second = seq.second;
            } else {
                PSSNode *n = nodes_map[&*I];
                // we must have this node, we found it!
                assert(n && "BUG");

                // if it is the first node in block,
                // update the block to keep it consistent
                if (n->predecessorsNum() == 0) {
                    blk.first = seq.first;
                    seq.second->addSuccessor(node);
                } else {
                    // insert the sequence before the node
                    n->insertSequenceBefore(seq);
                }
            }
        } else {
            // if the block is empty we just insert it
            blk.first = seq.first;
            blk.second = seq.second;
        }
    }

    unplacedInstructions.clear();
}

static bool memsetIsZeroInitialization(const llvm::IntrinsicInst *I)
{
    using namespace llvm;

    const Value *val = I->getOperand(1);
    if (const ConstantInt *C = dyn_cast<ConstantInt>(val)) {
        // we got memset that is not setting to 0 ...
        return C->isZero();
    }

    return false;
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

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createMemSet(const llvm::Instruction *Inst)
{
    PSSNode *val;
    if (memsetIsZeroInitialization(llvm::cast<llvm::IntrinsicInst>(Inst)))
        val = NULLPTR;
    else
        // if the memset is not 0-initialized, it does some
        // garbage into the pointer
        val = UNKNOWN_MEMORY;

    PSSNode *op = getOperand(Inst->getOperand(0)->stripInBoundsOffsets());
    // we need to make unknown offsets
    PSSNode *G = new PSSNode(pss::GEP, op, UNKNOWN_OFFSET);
    PSSNode *S = new PSSNode(pss::STORE, val, G);
    G->addSuccessor(S);

    return std::make_pair(G, S);
}

void LLVMPSSBuilder::checkMemSet(const llvm::Instruction *Inst)
{
    using namespace llvm;

    bool zeroed = memsetIsZeroInitialization(cast<IntrinsicInst>(Inst));
    if (!zeroed) {
        llvm::errs() << "WARNING: Non-0 memset: " << *Inst << "\n";
        return;
    }

    const Value *src = Inst->getOperand(0)->stripInBoundsOffsets();
    PSSNode *op = getOperand(src);

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(src)) {
        // if there cannot be stored a pointer, we can bail out here
        // XXX: what if it is alloca of generic mem (e. g. [100 x i8])
        // and we then store there a pointer? Or zero it and load from it?
        // like:
        // char mem[100];
        // void *ptr = (void *) mem;
        // void *p = *ptr;
        if (tyContainsPointer(AI->getAllocatedType()))
            op->setZeroInitialized();
    } else {
        // fallback: create a store that represents memset
        // the store will save null to ptr + UNKNOWN_OFFSET,
        // so we need to do:
        // G = GEP(op, UNKNOWN_OFFSET)
        // STORE(null, G)
        createIrrelevantInst(Inst, false /* recursive */);
    }
}

// return first and last nodes of the block
std::pair<PSSNode *, PSSNode *>&
LLVMPSSBuilder::buildPSSBlock(const llvm::BasicBlock& block)
{
    // create the item in built_blocks and use it as return value also
    std::pair<PSSNode *, PSSNode *>& ret = built_blocks[&block];

    // here we store sequence of nodes that will be created for each instruction
    std::pair<PSSNode *, PSSNode *> seq;

    PSSNode *last_node = nullptr;
    for (const llvm::Instruction& Inst : block) {
        if (!isRelevantInstruction(Inst)) {
            // check if it is a zeroing of memory,
            // if so, set the corresponding memory to zeroed
            if (llvm::isa<llvm::MemSetInst>(&Inst))
                checkMemSet(&Inst);

            continue;
        }

        seq = buildInstruction(Inst);
        assert(seq.first && seq.second
               && "Didn't created the instruction properly");

        // is this first created instruction?
        if (!last_node)
            ret.first = seq.first;
        else
            // else just add a successor
            last_node->addSuccessor(seq.first);

        // update last node that we created
        last_node = seq.second;
    }

    // set last node
    ret.second = seq.second;
    assert((ret.first && ret.second) || (!ret.first && !ret.second)
            && "BUG: inconsistent block");

    return ret;
}

static size_t blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                          std::pair<PSSNode *, PSSNode *>>& built_blocks,
                                 std::set<const llvm::BasicBlock *>& found_blocks,
                                 std::pair<PSSNode *, PSSNode *>& pssn,
                                 const llvm::BasicBlock& block)
{
    size_t num = 0;

    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {

         // we already processed this block? Then don't try to add the edges again
         if (!found_blocks.insert(*S).second)
            continue;

        std::pair<PSSNode *, PSSNode *>& succ = built_blocks[*S];
        assert((succ.first && succ.second) || (!succ.first && !succ.second));
        if (!succ.first) {
            // if we don't have this block built (there was no points-to
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // successor, add its successors as successors
            num += blockAddSuccessors(built_blocks, found_blocks, pssn, *(*S));
        } else {
            // add successor to the last nodes
            pssn.second->addSuccessor(succ.first);
            ++num;
        }
    }

    return num;
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::buildArguments(const llvm::Function& F)
{
    // create PHI nodes for arguments of the function. These will be
    // successors of call-node
    std::pair<PSSNode *, PSSNode *> ret;
    PSSNode *prev, *arg = nullptr;

    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A) {
        if (A->getType()->isPointerTy()) {
            prev = arg;

            arg = new PSSNode(pss::PHI, nullptr);
            addNode(&*A, arg);

            if (prev)
                prev->addSuccessor(arg);
            else
                ret.first = arg;
        }
    }

    // if the function has variable arguments,
    // then create the node for it and make it the last node
    if (F.isVarArg()) {
        ret.second = new PSSNode(pss::PHI, nullptr);
        if (arg)
            arg->addSuccessor(ret.second);
        else
            // we don't have any other argument than '...',
            // so this is first and last arg
            ret.first = ret.second;
    } else
        ret.second = arg;

    assert((ret.first && ret.second) || (!ret.first && !ret.second));

    return ret;
}

// build pointer state subgraph for given graph
// \return   root node of the graph
PSSNode *LLVMPSSBuilder::buildLLVMPSS(const llvm::Function& F)
{
    PSSNode *lastNode = nullptr;

    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    // XXX: do we need entry type?
    PSSNode *root = new PSSNode(pss::ENTRY);
    PSSNode *ret = new PSSNode(pss::NOOP);

    // now build the arguments of the function - if it has any
    std::pair<PSSNode *, PSSNode *> args = buildArguments(F);

    // add record to built graphs here, so that subsequent call of this function
    // from buildPSSBlock won't get stuck in infinite recursive call when
    // this function is recursive
    subgraphs_map[&F] = Subgraph(root, ret, args);

    // make arguments the entry block of the subgraphs (if there
    // are any arguments)
    if (args.first) {
        root->addSuccessor(args.first);
        lastNode = args.second;
    } else
        lastNode = root;

    assert(lastNode);

    PSSNode *first = nullptr;
    for (const llvm::BasicBlock& block : F) {
        std::pair<PSSNode *, PSSNode *>& nds = buildPSSBlock(block);

        if (!first) {
            // first block was not created at all? (it has not
            // pointer relevant instructions) - in that case
            // fake that the first block is the root itself
            if (!nds.first) {
                // nds is a reference
                nds.first = nds.second = root;
                first = root;
            } else {
                first = nds.first;

                // add correct successors. If we have arguments,
                // then connect the first block after arguments.
                // Otherwise connect them after the root node
                lastNode->addSuccessor(first);
            }
        }
    }

    // now we have created all the blocks, so place the instructions
    // that we were not able to place during building
    buildUnbuiltUses();
    addUnplacedInstructions();
    assert(unplacedInstructions.empty());

    std::vector<PSSNode *> rets;
    for (const llvm::BasicBlock& block : F) {
        std::pair<PSSNode *, PSSNode *>& pssn = built_blocks[&block];
        // if the block do not contain any points-to relevant instruction,
        // we returned (nullptr, nullptr)
        // FIXME: do not store such blocks at all
        assert((pssn.first && pssn.second) || (!pssn.first && !pssn.second));
        if (!pssn.first)
            continue;

        // add successors to this block (skipping the empty blocks).
        // To avoid infinite loops we use found_blocks container that will
        // server as a mark in BFS/DFS - the program should not contain
        // so many blocks that this could have some big overhead. If proven
        // otherwise later, we'll change this.
        std::set<const llvm::BasicBlock *> found_blocks;
        size_t succ_num = blockAddSuccessors(built_blocks, found_blocks,
                                             pssn, block);

        // if we have not added any successor, then the last node
        // of this block is a return node
        if (succ_num == 0 && pssn.second->getType() == pss::RETURN)
            rets.push_back(pssn.second);
    }

    // add successors edges from every real return to our artificial ret node
    // NOTE: if the function has infinite loop we won't have any return nodes,
    // so this assertion must not hold
    //assert(!rets.empty() && "BUG: Did not find any return node in function");
    for (PSSNode *r : rets)
        r->addSuccessor(ret);

    // add arguments to PHI nodes. We need to do that after the graph is
    // entirely built, because during building the arguments may not
    // be built yet
    addPHIOperands(F);

    return root;
}

PSSNode *LLVMPSSBuilder::buildLLVMPSS()
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    std::pair<PSSNode *, PSSNode *> glob = buildGlobals();

    // now we can build rest of the graph
    PSSNode *root = buildLLVMPSS(*F);

    // do we have any globals at all? If so, insert them at the begining
    // of the graph
    // FIXME: we do not need to process them later,
    // should we do it somehow differently?
    // something like 'static nodes' in PSS...
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);
        root = glob.first;
    }

    // must have placed all the unplaced instructions
    assert(unplacedInstructions.empty());

    return root;
}

PSSNode *
LLVMPSSBuilder::handleGlobalVariableInitializer(const llvm::Constant *C,
                                                PSSNode *node)
{
    using namespace llvm;
    PSSNode *last = node;

    // if the global is zero initialized, just set the zeroInitialized flag
    if (isa<ConstantPointerNull>(C)
        || isa<ConstantAggregateZero>(C)) {
        node->setZeroInitialized();
    } else if (C->getType()->isAggregateType()) {
        uint64_t off = 0;
        for (auto I = C->op_begin(), E = C->op_end(); I != E; ++I) {
            const Value *val = *I;
            Type *Ty = val->getType();

            if (Ty->isPointerTy()) {
                PSSNode *op = getOperand(val);
                PSSNode *target = new PSSNode(CONSTANT, node, off);
                // FIXME: we're leaking the target
                // NOTE: mabe we could do something like
                // CONSTANT_STORE that would take Pointer instead of node??
                // PSSNode(CONSTANT_STORE, op, Pointer(node, off)) or
                // PSSNode(COPY, op, Pointer(node, off))??
                PSSNode *store = new PSSNode(STORE, op, target);
                store->insertAfter(last);
                last = store;
            }

            off += DL->getTypeAllocSize(Ty);
        }
    } else if (isa<ConstantExpr>(C) || isa<Function>(C)) {
       if (C->getType()->isPointerTy()) {
           PSSNode *value = getOperand(C);
           assert(value->pointsTo.size() == 1 && "BUG: We should have constant");
           // FIXME: we're leaking the target
           PSSNode *store = new PSSNode(STORE, value, node);
           store->insertAfter(last);
           last = store;
       }
    } else if (!isa<ConstantInt>(C)) {
        llvm::errs() << *C << "\n";
        llvm::errs() << "ERROR: ^^^ global variable initializer not handled\n";
    }

    return last;
}

std::pair<PSSNode *, PSSNode *> LLVMPSSBuilder::buildGlobals()
{
    PSSNode *cur = nullptr, *prev, *first = nullptr;
    // create PSS nodes
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = new PSSNode(pss::ALLOC);
        addNode(&*I, cur);

        if (prev)
            prev->addSuccessor(cur);
        else
            first = cur;
    }

    // only now handle the initializers - we need to have then
    // built, because they can point to each other
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        // handle globals initialization
        const llvm::GlobalVariable *GV
                            = llvm::dyn_cast<llvm::GlobalVariable>(&*I);
        if (GV && GV->hasInitializer() && !GV->isExternallyInitialized()) {
            const llvm::Constant *C = GV->getInitializer();
            PSSNode *node = nodes_map[&*I];
            assert(node && "BUG: Do not have global variable");
            cur = handleGlobalVariableInitializer(C, node);
        }
    }

    assert((!first && !cur) || (first && cur));
    return std::pair<PSSNode *, PSSNode *>(first, cur);
}

} // namespace pss
} // namespace analysis
} // namespace dg
