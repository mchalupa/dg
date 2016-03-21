#include <cassert>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/raw_os_ostream.h>

#include "analysis/PSS.h"
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

#ifdef DEBUG_ENABLED
static std::string
getInstName(const llvm::Value *val)
{
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
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

void setName(const llvm::Value *val, PSSNode *node, const char *prefix = nullptr)
{
    const char *name = __get_name(val, prefix);
    node->setName(name);
}

void setName(const char *name, PSSNode *node, const char *prefix = nullptr)
{
    if (prefix) {
        std::string nm;
        nm.append(prefix);
        nm.append(name);
        node->setName(nm.c_str());
    } else
        node->setName(name);
}

#else
void setName(const llvm::Value *val, PSSNode *node, const char *prefix = nullptr)
{
}

void setName(const char *name, PSSNode *node, const char *prefix = nullptr)
{
}
#endif

enum MemAllocationFuncs {
    NONEMEM = 0,
    MALLOC,
    CALLOC,
    ALLOCA,
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
        // FIXME
        assert(0 && "realloc not implemented yet");

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
    PSSNode *op = nodes_map[llvmOp];
    if (!op) {
        // is this recursively created expression? If so, get the pointer for it
        if (isa<ConstantExpr>(llvmOp)) {
            return getConstantExprPointer(cast<ConstantExpr>(llvmOp));
        } else {
            errs() << *llvmOp << "\n";
            errs() << *BC << "\n";
            assert(0 && "Unsupported bitcast");
        }
    } else {
        assert(op->pointsTo.size() == 1
               && "Constant BitCast with not only one pointer");

        return *op->pointsTo.begin();
    }
}

Pointer LLVMPSSBuilder::handleConstantGep(const llvm::GetElementPtrInst *GEP)
{
    using namespace llvm;

    const Value *op = GEP->getPointerOperand();
    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    // get operand PSSNode - if it exists
    PSSNode *opNode = nodes_map[op];

    // we dont have the operand node... is it constant or constant expr?
    if (!opNode) {
        // is this recursively created expression? If so, get the pointer for it
        if (isa<ConstantExpr>(op)) {
            pointer = getConstantExprPointer(cast<ConstantExpr>(op));
        } else {
            errs() << *op << "\n";
            errs() << *GEP << "\n";
            assert(0 && "Unsupported constant GEP");
        }
    } else {
            assert(opNode->pointsTo.size() == 1
                   && "Constant node has more that 1 pointer");
            pointer = *(opNode->pointsTo.begin());
    }

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
    PSSNode *node = new PSSNode(pss::CONSTANT, ptr);

    addNode(CE, node);
    setName(CE, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::getConstant(const llvm::Value *val)
{
    if (llvm::isa<llvm::ConstantPointerNull>(val)) {
        return NULLPTR;
    } else if (const llvm::ConstantExpr *CE
                    = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
        return createConstantExpr(CE);
    } else if (const llvm::Function *F
                    = llvm::dyn_cast<llvm::Function>(val)) {
        PSSNode *ret = new PSSNode(FUNCTION);
        addNode(val, ret);
        setName(F->getName().data(), ret);

        return ret;
    } else {
        llvm::errs() << "Unspported constant: " << *val << "\n";
        abort();
    }
}

PSSNode *LLVMPSSBuilder::getOperand(const llvm::Value *val)
{
    PSSNode *op = nodes_map[val];
    if (!op)
        op = getConstant(val);

    // if the operand is a call, use the return node of the call instead
    // - this is the one that contains returned pointers
    if (op->getType() == pss::CALL)
        op = op->getOperand(0);

    assert(op && "Did not find an operand");
    return op;
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

    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
        // if the size cannot be expressed as an uint64_t,
        // just set it to 0 (that means unknown)
        if (size == ~((uint64_t) 0))
            size = 0;

        // if this is call to calloc, the size is given
        // in the first argument too
        if (type == CALLOC) {
            C = dyn_cast<ConstantInt>(CInst->getOperand(0));
            if (C) {
                size2 = C->getLimitedValue();
                if (size2 == ~((uint64_t) 0))
                    size2 = 0;
                else
                    // OK, if getting the size fails, we end up with
                    // just 1 * size - still better than 0 and UNKNOWN
                    // (it may be cropped later anyway)
                    size *= size2;
            }
        }
    }

    node->setSize(size);
    return node;
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createDynamicMemAlloc(const llvm::CallInst *CInst, int type)
{
    PSSNode *node = createDynamicAlloc(CInst, type);
    addNode(CInst, node);
    setName(CInst, node);

    // we return (node, node), so that the parent function
    // will seamlessly connect this node into the graph
    return std::make_pair(node, node);
}

std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createCallToFunction(const llvm::CallInst *CInst,
                                     const llvm::Function *F)
{
    PSSNode *callNode, *returnNode;

    returnNode = new PSSNode(pss::CALL_RETURN, nullptr);
    // we can use the arguments of the call as we want - store
    // there the return node (that is the one that will contain
    // returned pointers) so that we can use it whenever we need
    callNode = new PSSNode(pss::CALL, returnNode, nullptr);
    // the same with return node - we would like to know
    // to which call the reutrn belongs
    returnNode->addOperand(callNode);

    setName(CInst, callNode);
    setName(CInst, returnNode, "RET");

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
            arg = arg->getSingleSuccessor();
        }
    }

    // handle value returned from the function if it is a pointer
    if (CInst->getType()->isPointerTy()) {
        // return node is like a PHI node
        for (PSSNode *r : subg.ret->getPredecessors())
            // we're interested only in the nodes that return some value
            // from subprocedure, not for all nodes that have no successor
            if (r->getType() == pss::RETURN)
                returnNode->addOperand(r);
    }

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

// create subgraph or add edges to already existing subgraph,
// return the CALL node (the first) and the RETURN node (the second),
// so that we can connect them into the PSS
std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::createCall(const llvm::Instruction *Inst)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();

    const Function *func = dyn_cast<Function>(calledVal);
    if (func) {
        /// memory allocation (malloc, calloc, etc.)
        int type;
        if ((type = getMemAllocationFunc(func))) {
            // NOTE: must be before func->size() == 0 condition,
            // since malloc and similar are undefined too
            return createDynamicMemAlloc(CInst, type);
        } else if (func->size() == 0) {
            // the function is not declared, just put there
            // the call node
            // XXX: don't do that when the function does not return
            // the pointer, it has no meaning
            PSSNode *node = new PSSNode(pss::CALL, nullptr);
            setName(CInst, node);
            return std::make_pair(node, node);
        } else if (func->isIntrinsic()) {
            assert(0 && "Intrinsic function not implemented yet");
        } else {
            return createOrGetSubgraph(CInst, func);
        }
    } else {
        // function pointer call
        PSSNode *op = getOperand(calledVal);
        PSSNode *call_funcptr = new PSSNode(pss::CALL_FUNCPTR, op);
        PSSNode *ret_call = new PSSNode(RETURN, call_funcptr, nullptr);

        // the first operand is the pointer node, but the rest of the operands
        // are free for us to use, store there the return node from the call
        // - as we do in the normal call node
        call_funcptr->addOperand(ret_call);
        call_funcptr->addSuccessor(ret_call);
        addNode(CInst, call_funcptr);
        setName(CInst, call_funcptr, "funcptr");
        setName(CInst, ret_call, "RETURN");

        return std::make_pair(call_funcptr, ret_call);
    }
}

PSSNode *LLVMPSSBuilder::createAlloc(const llvm::Instruction *Inst)
{
    PSSNode *node = new PSSNode(pss::ALLOC);
    addNode(Inst, node);
    setName(Inst, node);

    const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(Inst);
    if (AI) {
        uint64_t size = getAllocatedSize(AI->getAllocatedType(), DL);
        node->setSize(size);
    }

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createStore(const llvm::Instruction *Inst)
{
    const llvm::Value *valOp = Inst->getOperand(0);

    // the value needs to be a pointer - we call this function only under
    // this condition
    assert(valOp->getType()->isPointerTy() && "BUG: Store value is not a pointer");

    PSSNode *op1 = getOperand(valOp);
    PSSNode *op2 = getOperand(Inst->getOperand(1));

    PSSNode *node = new PSSNode(pss::STORE, op1, op2);
    addNode(Inst, node);
    setName(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createLoad(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1 = getOperand(op);
    PSSNode *node = new PSSNode(pss::LOAD, op1);

    addNode(Inst, node);
    setName(Inst, node);

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
    setName(Inst, node);

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
    setName(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createCast(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1 = getOperand(op);
    PSSNode *node = new PSSNode(pss::CAST, op1);

    addNode(Inst, node);
    setName(Inst, node);

    assert(node);
    return node;
}

PSSNode *LLVMPSSBuilder::createReturn(const llvm::Instruction *Inst)
{
    PSSNode *op1 = nullptr;

    // we create even void and non-pointer return nodes,
    // since these modify CFG (they won't bear any
    // points-to information though)
    // XXX is that needed?
    if (!Inst->getType()->isVoidTy()) {
        const llvm::Value *op = Inst->getOperand(0);

        if (op->getType()->isPointerTy())
            op1 = getOperand(op);
    }

    PSSNode *node = new PSSNode(pss::RETURN, op1, nullptr);
    addNode(Inst, node);
    setName(Inst, node, "RETURN ");

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

// return first and last nodes of the block
std::pair<PSSNode *, PSSNode *>
LLVMPSSBuilder::buildPSSBlock(const llvm::BasicBlock& block)
{
    using namespace llvm;

    std::pair<PSSNode *, PSSNode *> ret(nullptr, nullptr);
    PSSNode *prev_node;
    PSSNode *node = nullptr;
    for (const Instruction& Inst : block) {
        prev_node = node;

        switch(Inst.getOpcode()) {
            case Instruction::Alloca:
                node = createAlloc(&Inst);
                break;
            case Instruction::Store:
                // create only nodes that store pointer to another
                // pointer. We don't care about stores of non-pointers
                if (Inst.getOperand(0)->getType()->isPointerTy())
                    node = createStore(&Inst);
                break;
            case Instruction::Load:
                if (Inst.getType()->isPointerTy())
                    node = createLoad(&Inst);
                break;
            case Instruction::GetElementPtr:
                node = createGEP(&Inst);
                break;
            case Instruction::Select:
                if (Inst.getType()->isPointerTy())
                    node = createSelect(&Inst);
                break;
            case Instruction::BitCast:
                node = createCast(&Inst);
                break;
            case Instruction::Ret:
                    node = createReturn(&Inst);
                break;
            case Instruction::Call:
                if (!isRelevantCall(&Inst))
                    break;

                std::pair<PSSNode *, PSSNode *> subg = createCall(&Inst);
                if (prev_node)
                    prev_node->addSuccessor(subg.first);
                else
                    // graphs starts with function call?
                    ret.first = subg.first;

                // new nodes will connect to the return node
                node = prev_node = subg.second;

                break;
        }

        // first instruction
        if (node && !prev_node)
            ret.first = node;

        if (prev_node && prev_node != node)
            prev_node->addSuccessor(node);
    }

    // last node
    ret.second = node;

    return ret;
}

static size_t blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                          std::pair<PSSNode *, PSSNode *>>& built_blocks,
                                 std::pair<PSSNode *, PSSNode *>& pssn,
                                 const llvm::BasicBlock& block)
{
    size_t num = 0;

    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {
        std::pair<PSSNode *, PSSNode *>& succ = built_blocks[*S];
        assert((succ.first && succ.second) || (!succ.first && !succ.second));
        if (!succ.first) {
            // if we don't have this block built (there was no points-to
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // successor, add its successors as successors
            num += blockAddSuccessors(built_blocks, pssn, *(*S));
        } else {
            // add successor to the last nodes
            pssn.second->addSuccessor(succ.first);
            ++num;
        }
    }

    return num;
}

std::pair<PSSNode *, PSSNode *> LLVMPSSBuilder::buildArguments(const llvm::Function& F)
{
    // create PHI nodes for arguments of the function. These will be
    // successors of call-node
    std::pair<PSSNode *, PSSNode *> ret;
    int idx = 0;
    PSSNode *prev, *arg = nullptr;

    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A, ++idx) {
        if (A->getType()->isPointerTy()) {
            prev = arg;

            arg = new PSSNode(pss::PHI, nullptr);
            addNode(&*A, arg);

            if (prev)
                prev->addSuccessor(arg);
            else
                ret.first = arg;

            setName(&*A, arg, "ARG phi ");
        }
    }

    ret.second = arg;
    assert((ret.first && ret.second) || (!ret.first && !ret.second));

    return ret;
}

// build pointer state subgraph for given graph
// \return   root node of the graph
PSSNode *LLVMPSSBuilder::buildLLVMPSS(const llvm::Function& F)
{
    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, std::pair<PSSNode *, PSSNode *>> built_blocks;
    PSSNode *lastNode = nullptr;

    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    // XXX: do we need entry type?
    PSSNode *root = new PSSNode(pss::ENTRY);
    PSSNode *ret = new PSSNode(pss::NOOP);

    setName(F.getName().data(), root, "ENTRY ");
    setName(F.getName().data(), ret, "RET (unified) ");

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
        std::pair<PSSNode *, PSSNode *> nds = buildPSSBlock(block);

        if (!first) {
            // first block was not created at all? (it has not
            // pointer relevant instructions) - in that case
            // fake that the first block is the root itself
            if (!nds.first) {
                // if the function has arguments, then it has
                // single entry block where it copies the values
                // of arguments to local variables - thus this
                // assertions must hold
                assert(!args.first);
                assert(lastNode == root);

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

        built_blocks[&block] = nds;
    }

    std::vector<PSSNode *> rets;
    for (const llvm::BasicBlock& block : F) {
        std::pair<PSSNode *, PSSNode *>& pssn = built_blocks[&block];
        // if the block do not contain any points-to relevant instruction,
        // we returned (nullptr, nullptr)
        // FIXME: do not store such blocks at all
        assert((pssn.first && pssn.second) || (!pssn.first && !pssn.second));
        if (!pssn.first)
            continue;

        // add successors to this block (skipping the empty blocks)
        size_t succ_num = blockAddSuccessors(built_blocks, pssn, block);

        // if we have not added any successor, then the last node
        // of this block is a return node
        if (succ_num == 0)
            rets.push_back(pssn.second);
    }

    // add successors edges from every real return to our artificial ret node
    assert(!rets.empty() && "BUG: Did not find any return node in function");
    for (PSSNode *r : rets)
        r->addSuccessor(ret);

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

    // do we have any globals at all? If so, insert them at the begining of the graph
    // FIXME: we do not need to process them later, should we do it somehow differently?
    // something like 'static nodes' in PSS...
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);
        root = glob.first;
    }

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
                PSSNode *target = new PSSNode(CONSTANT, Pointer(node, off));
                // FIXME: we're leaking the target
                // NOTE: mabe we could do something like
                // CONSTANT_STORE that would take Pointer instead of node??
                // PSSNode(CONSTANT_STORE, op, Pointer(node, off)) or
                // PSSNode(COPY, op, Pointer(node, off))??
                PSSNode *store = new PSSNode(STORE, op, target);
                store->insertAfter(last);
                last = store;

                // FIXME: uauauagghh that's ugly!
                setName(C, store, ((std::string("INIT ") + node->getName()
                                + "[" + std::to_string(off) + "] -> "
                                + getInstName(val)).c_str()));
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

           // FIXME: uauauagghh that's ugly!
           const Pointer& ptr = *(value->pointsTo.begin());
           setName(C, store, ((std::string("INIT ") + node->getName()
                           + " -> " + ptr.target->getName() + " + "
                           + std::to_string(*ptr.offset)).c_str()));
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
        setName(&*I, cur);

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
