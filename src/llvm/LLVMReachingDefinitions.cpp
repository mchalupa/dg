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
#include "LLVMReachingDefinitions.h"

#ifdef DEBUG_ENABLED
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#endif

namespace dg {
namespace analysis {
namespace rd {

#if 0
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

static uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return 0;

    return DL->getTypeAllocSize(Ty);
}

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

RDNode *LLVMRDBuilder::createAlloc(const llvm::Instruction *Inst)
{
    RDNode *node = new RDNode(ALLOC);
    addNode(Inst, node);

    return node;
}

RDNode *LLVMRDBuilder::createReturn(const llvm::Instruction *Inst)
{
    RDNode *node = new RDNode(RETURN);
    addNode(Inst, node);

    return node;
}

RDNode *LLVMRDBuilder::createStore(const llvm::Instruction *Inst)
{
    RDNode *node = new RDNode(STORE);
    addNode(Inst, node);

    pss::PSSNode *pts = PTA->getPointsTo(Inst->getOperand(1));
    assert(pts && "Don't have the points-to information for store");

    if (pts->pointsTo.empty()) {
        llvm::errs() << "ERROR: empty STORE points-to: " << *Inst << "\n";
        abort();
    }

    for (const pss::Pointer& ptr: pts->pointsTo) {
        // XXX we should at least warn?
        if (ptr.isNull())
            continue;

        if (ptr.isUnknown()) {
            node->addDef(UNKNOWN_MEMORY);
            continue;
        }

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        RDNode *ptrNode = nodes_map[ptrVal];
        //assert(ptrNode && "Don't have created node for pointer's target");
        if (!ptrNode) {
            llvm::errs() << *ptrVal << "\n";
            llvm::errs() << "Don't have created node for pointer's target\n";
            continue;
        }

        uint64_t size = getAllocatedSize(Inst->getOperand(0)->getType(), DL);
        if (size == 0)
            size = UNKNOWN_OFFSET;

        //llvm::errs() << *Inst << " DEFS >> " << ptr.target->getName() << " ["
        //             << *ptr.offset << " - " << *ptr.offset + size - 1 << "\n";
        node->addDef(ptrNode, ptr.offset, size,
                     pts->pointsTo.size() == 1 /* strong update */);
    }

    assert(node);
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
                case Intrinsic::memset:
                case Intrinsic::vastart:
                    return true;
                default:
                    return false;
            }
        }

        // undefined function
        return true;
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

// return first and last nodes of the block
std::pair<RDNode *, RDNode *>
LLVMRDBuilder::buildBlock(const llvm::BasicBlock& block)
{
    using namespace llvm;

    std::pair<RDNode *, RDNode *> ret(nullptr, nullptr);
    RDNode *prev_node;
    // the first node is dummy and serves as a phi from previous
    // blocks so that we can have proper mapping
    RDNode *node = new RDNode(PHI);
    ret.first = node;

    for (const Instruction& Inst : block) {
        // some nodes may have nullptr as mapping,
        // that means that there are no reaching definitions
        // (well, no nodes to be precise) to map that on
        mapping[&Inst] = node;
        prev_node = node;

        switch(Inst.getOpcode()) {
            case Instruction::Alloca:
                // we need alloca's as target to DefSites
                node = createAlloc(&Inst);
                break;
            case Instruction::Store:
                node = createStore(&Inst);
                break;
            case Instruction::Ret:
                // we need create returns, since
                // these modify CFG and thus data-flow
                // FIXME: add new type of node NOOP,
                // and optimize it away later
                node = createReturn(&Inst);
                break;
            case Instruction::Call:
                if (!isRelevantCall(&Inst))
                    break;

                std::pair<RDNode *, RDNode *> subg = createCall(&Inst);
                prev_node->addSuccessor(subg.first);

                // new nodes will connect to the return node
                node = prev_node = subg.second;

                break;
        }

        if (prev_node && prev_node != node)
            prev_node->addSuccessor(node);
    }

    // last node
    ret.second = node;

    return ret;
}

static size_t blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                          std::pair<RDNode *, RDNode *>>& built_blocks,
                                 std::pair<RDNode *, RDNode *>& pssn,
                                 const llvm::BasicBlock& block)
{
    size_t num = 0;

    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {
        std::pair<RDNode *, RDNode *>& succ = built_blocks[*S];
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

std::pair<RDNode *, RDNode *>
LLVMRDBuilder::createCallToFunction(const llvm::CallInst *CInst,
                                     const llvm::Function *F)
{
    RDNode *callNode, *returnNode;

    // dummy nodes for easy generation
    returnNode = new RDNode(CALL_RETURN);
    callNode = new RDNode(CALL);

    // reuse built subgraphs if available
    Subgraph subg = subgraphs_map[F];
    if (!subg.root) {
        // create new subgraph
        buildFunction(*F);
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

    return std::make_pair(callNode, returnNode);
}

RDNode *LLVMRDBuilder::buildFunction(const llvm::Function& F)
{
    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, std::pair<RDNode *, RDNode *>> built_blocks;

    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    RDNode *root = new RDNode(NOOP);
    RDNode *ret = new RDNode(NOOP);

    // add record to built graphs here, so that subsequent call of this function
    // from buildPSSBlock won't get stuck in infinite recursive call when
    // this function is recursive
    subgraphs_map[&F] = Subgraph(root, ret);

    RDNode *first = nullptr;
    for (const llvm::BasicBlock& block : F) {
        std::pair<RDNode *, RDNode *> nds = buildBlock(block);
        assert((nds.first && nds.second) || (!nds.first && !nds.second));

        if (nds.first) {
            built_blocks[&block] = nds;
            if (!first)
                first = nds.first;
        }
    }

    assert(first);
    root->addSuccessor(first);

    std::vector<RDNode *> rets;
    for (const llvm::BasicBlock& block : F) {
        auto it = built_blocks.find(&block);
        if (it == built_blocks.end())
            continue;

        std::pair<RDNode *, RDNode *>& pssn = it->second;
        assert((pssn.first && pssn.second) || (!pssn.first && !pssn.second));
        if (!pssn.first)
            continue;

        // add successors to this block (skipping the empty blocks)
        // FIXME: this function is shared with PSS, factor it out
        size_t succ_num = blockAddSuccessors(built_blocks, pssn, block);

        // if we have not added any successor, then the last node
        // of this block is a return node
        if (succ_num == 0 && pssn.second->getType() == RETURN)
            rets.push_back(pssn.second);
    }

    // add successors edges from every real return to our artificial ret node
    for (RDNode *r : rets)
        r->addSuccessor(ret);

    return root;

}

RDNode *LLVMRDBuilder::createUndefinedCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    RDNode *node = new RDNode(CALL);
    addNode(CInst, node);

    // every pointer we pass into the undefined call may be defined
    // in the function
    for (unsigned int i = 0; i < CInst->getNumArgOperands(); ++i)
    {
        const Value *llvmOp = CInst->getArgOperand(i);
        if (!llvmOp->getType()->isPointerTy())
            continue;

        pss::PSSNode *pts = PTA->getPointsTo(llvmOp);
        assert(pts && "No points-to information");
        for (const pss::Pointer& ptr : pts->pointsTo) {
            if (ptr.isNull())
                continue;

            const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
            RDNode *target = nodes_map[ptrVal];
            assert(target && "Don't have pointer target for call argument");

            // this call may define this memory
            node->addDef(target, UNKNOWN_OFFSET, UNKNOWN_OFFSET);
        }
    }

    return node;
}

RDNode *LLVMRDBuilder::createIntrinsicCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(CInst);
    const Value *dest;
    const Value *lenVal;

    RDNode *ret = new RDNode(CALL);
    addNode(CInst, ret);

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::memset:
            // memcpy/set <dest>, <src/val>, <len>
            dest = I->getOperand(0);
            lenVal = I->getOperand(2);
            break;
        case Intrinsic::vastart:
            // we create this node because this nodes works
            // as ALLOC in points-to, so we can have
            // reaching definitions to that
            ret->addDef(ret, 0, UNKNOWN_OFFSET);
            return ret;
        default:
            return createUndefinedCall(CInst);
    }

    pss::PSSNode *pts = PTA->getPointsTo(dest);
    assert(pts && "No points-to information");

    uint64_t len;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(lenVal))
        len = C->getLimitedValue();

    for (const pss::Pointer& ptr : pts->pointsTo) {
        uint64_t from;
        uint64_t to;
        if (ptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, use whole memory
            from = UNKNOWN_OFFSET;
            len = UNKNOWN_OFFSET;
        } else {
            from = *ptr.offset;
        }

        if (len != ~((uint64_t) 0))
            // do not allow overflow
            to = from + len;
        else
            to = UNKNOWN_OFFSET;

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        RDNode *target = nodes_map[ptrVal];
        assert(target && "Don't have pointer target for intrinsic call");

        // add the definition
        ret->addDef(target, from, to, true /* strong update */);
    }

    return ret;
}

std::pair<RDNode *, RDNode *>
LLVMRDBuilder::createCall(const llvm::Instruction *Inst)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();

    const Function *func = dyn_cast<Function>(calledVal);
    if (func) {
        /// memory allocation (malloc, calloc, etc.)
        int type;
        if (func->isIntrinsic()) {
            RDNode *n = createIntrinsicCall(CInst);
            return std::make_pair(n, n);
        } else if ((type = getMemAllocationFunc(func))) {
            // NOTE: func->size() == 0 holds even for malloc,
            // do we need the first part of the condition?
            RDNode *n = createAlloc(CInst);
            return std::make_pair(n, n);
        } else if (func->size() == 0) {
            RDNode *n = createUndefinedCall(CInst);
            return std::make_pair(n, n);
        } else {
            std::pair<RDNode *, RDNode *> cf
                = createCallToFunction(CInst, func);
            addNode(CInst, cf.first);
            return cf;
        }
    } else {
        // function pointer call
        pss::PSSNode *op = PTA->getPointsTo(calledVal);
        assert(op && "Don't have points-to information");
        assert(!op->pointsTo.empty() && "Don't have pointer to the func");

        RDNode *call_funcptr = nullptr, *ret_call = nullptr;

        if (op->pointsTo.size() > 1) {
            call_funcptr = new RDNode(CALL);
            ret_call = new RDNode(CALL_RETURN);

            addNode(CInst, call_funcptr);

            for (const pss::Pointer& ptr : op->pointsTo) {
                // XXX: + unknown pointers
                if (ptr.isNull())
                    continue;

                // check if it is a function (varargs may
                // introduce some unprecision to func. pointers)
                if (!isa<Function>(ptr.target->getUserData<Value>()))
                    continue;

                const Function *F = ptr.target->getUserData<Function>();
                std::pair<RDNode *, RDNode *> cf
                    = createCallToFunction(CInst, F);

                // connect the graphs
                call_funcptr->addSuccessor(cf.first);
                cf.second->addSuccessor(ret_call);
            }
        } else {
            // don't add redundant nodes if not needed
            const llvm::Function *F
                = (op->pointsTo.begin())->target->getUserData<llvm::Function>();
            std::pair<RDNode *, RDNode *> cf = createCallToFunction(CInst, F);
            call_funcptr = cf.first;
            ret_call = cf.second;
        }

        assert(call_funcptr && ret_call);
        return std::make_pair(call_funcptr, ret_call);
    }
}

RDNode *LLVMRDBuilder::build()
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    std::pair<RDNode *, RDNode *> glob = buildGlobals();

    // now we can build rest of the graph
    RDNode *root = buildFunction(*F);
    assert(root);

    // do we have any globals at all? If so, insert them at the begining
    // of the graph
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);

        assert(root->successorsNum() > 0);
        root = glob.first;
    }

    return root;
}

std::pair<RDNode *, RDNode *> LLVMRDBuilder::buildGlobals()
{
    RDNode *cur = nullptr, *prev, *first = nullptr;
    // create PSS nodes
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = new RDNode(ALLOC);
        addNode(&*I, cur);

        if (prev)
            prev->addSuccessor(cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return std::pair<RDNode *, RDNode *>(first, cur);
}

} // namespace rd
} // namespace analysis
} // namespace dg
