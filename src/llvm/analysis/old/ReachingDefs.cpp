#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "llvm/LLVMNode.h"
#include "llvm/LLVMDependenceGraph.h"
#include "llvm-debug.h"

#include "ReachingDefs.h"
#include "DefMap.h"
#include "AnalysisGeneric.h"

#include "analysis/DFS.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMReachingDefsAnalysis::LLVMReachingDefsAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg), DL(new DataLayout(dg->getModule()))
{
}

Pointer LLVMReachingDefsAnalysis::getConstantExprPointer(ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

LLVMNode *LLVMReachingDefsAnalysis::getOperand(LLVMNode *node,
                                               Value *val,
                                               unsigned int idx)
{
    return dg::analysis::getOperand(node, val, idx, DL);
}

// FIXME don't duplicate the code from DefUse.cpp
static DefMap *getDefMap(LLVMNode *n)
{
    DefMap *r = n->getData<DefMap>();
    if (!r) {
        r = new DefMap();
        n->setData(r);
    }

    // must always have
    assert(r);

    return r;
}

/// --------------------------------------------------
//   Reaching definitions analysis
/// --------------------------------------------------

static bool handleParam(const Pointer& ptr, LLVMNode *to,
                        DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;
    // check if memory that is pointed by ptr
    // with _arbitrary_ offset is defined in the
    // subprocedure
    auto bounds = subgraph_df->getObjectRange(ptr);
    for (auto it = bounds.first; it != bounds.second; ++it) {
        changed |= df->add(it->first, to);
    }

    return changed;
}

static bool handleParam(LLVMNode *node, LLVMNode *to,
                        DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;
    for (const Pointer& ptr : node->getPointsTo()) {
        changed |= handleParam(ptr, to, df, subgraph_df);

        // handle also the memory pointers, if we define some memory
        // in subprocedure, we'd like to propagate it to the callee
        if (!ptr.isKnown())
            continue;

        for (auto memit : ptr.obj->pointsTo)
            for (const Pointer& memptr : memit.second)
                changed |= handleParam(memptr, to, df, subgraph_df);
    }

    return changed;
}

static bool handleParamsGlobals(LLVMDependenceGraph *dg,
                                LLVMDGParameters *params,
                                DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;
    for (auto I = params->global_begin(), E = params->global_end(); I != E; ++I) {
        LLVMDGParameter& p = I->second;

        // get the global node, it contains the points-to set
        LLVMNode *glob = dg->getNode(I->first);
        if (!glob) {
            errs() << "ERR: no global node for param\n";
            continue;
        }

        changed |= handleParam(glob, p.out, df, subgraph_df);
    }

    return changed;
}

static bool handleDynMemoryParams(LLVMDependenceGraph *subgraph, LLVMDGParameters *params,
                                  DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;

    LLVMDGParameters *formal = subgraph->getParameters();
    if (!formal)
        return false;

    // operand[0] is the called func
    for (auto it : *formal) {
        // FIXME Probably will be best add to DGParameters another
        // container for mem. allocation params, to keep it
        // separate so that we don't need to do this
        if (isa<CallInst>(it.first)) {
            // the formal in param contains the points-to set
            LLVMDGParameter *actprm = params->find(it.first);
            assert(actprm && "No actual param for dyn. mem.");
            changed |= handleParam(it.second.in, actprm->out, df, subgraph_df);
        }
    }

    return changed;
}

static bool handleVarArgParams(LLVMDependenceGraph *subgraph,
                               DefMap *df, DefMap *subgraph_df)
{

    LLVMDGParameters *formal = subgraph->getParameters();
    if (!formal)
        return false;

    LLVMDGParameter *vaparam = formal->getVarArg();
    assert(vaparam && "No va param in vararg function");

    return handleParam(vaparam->in, vaparam->out, df, subgraph_df);
}

static bool handleParams(LLVMNode *callNode, unsigned vararg,
                         LLVMDGParameters *params,
                         DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;

    // operand[0] is the called func
    for (int i = 1, e = callNode->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = callNode->getOperand(i);
        if (!op)
            continue;

        if (!op->isPointerTy())
            continue;

        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
#ifdef DEBUG_ENABLED
            if (i - 1 < (int) vararg)
                DBG("ERR: no actual param for " << *op->getKey());
#endif
            continue;
        }

        changed |= handleParam(op, p->out, df, subgraph_df);
    }

    return changed;
}

static bool handleParams(LLVMNode *callNode, LLVMDependenceGraph *subgraph,
                         DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;

    // get actual parameters (operands) and for every pointer in there
    // check if the memory location it points to gets defined
    // in the subprocedure
    LLVMDGParameters *params = callNode->getParameters();
    // if we have params, process params
    if (!params)
        return false;

    const Function *func = cast<Function>(subgraph->getEntry()->getKey());
    unsigned vararg = 0;
    // set this only with vararg functions, for revealing the bugs
    if (func->isVarArg())
        vararg = func->arg_size();

    changed |= handleParams(callNode, vararg, params, df, subgraph_df);
    changed |= handleParamsGlobals(callNode->getDG(), params, df, subgraph_df);
    changed |= handleDynMemoryParams(subgraph, params, df, subgraph_df);
    if (vararg != 0)
        changed |= handleVarArgParams(subgraph, df, subgraph_df);

    return changed;
}


bool LLVMReachingDefsAnalysis::handleUndefinedCall(LLVMNode *callNode,
                                                   CallInst *CI,
                                                   DefMap *df)
{
    bool changed = false;
    for (unsigned n = 1, e = callNode->getOperandsNum(); n < e; ++n) {
        Value *llvmOp = CI->getOperand(n - 1);
        if (!llvmOp->getType()->isPointerTy())
            continue;

        if (isa<Constant>(llvmOp->stripInBoundsOffsets()))
            continue;

        LLVMNode *op = getOperand(callNode, llvmOp, n);
        assert(op && "unhandled pointer operand in undef call");

        // with undefined call we must assume that any
        // memory that was passed via pointer was modified
        // and on unknown offset
        // XXX we should handle external globals too
        for (const Pointer& ptr : op->getPointsTo())
            changed |= df->add(Pointer(ptr.obj, UNKNOWN_OFFSET), callNode);
    }

    return changed;
}

bool LLVMReachingDefsAnalysis::handleIntrinsicCall(LLVMNode *callNode,
                                                   CallInst *CI,
                                                   DefMap *df)
{
    bool changed = false;
    IntrinsicInst *I = cast<IntrinsicInst>(CI);
    Value *dest;

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::memset:
            dest = I->getOperand(0);
            break;
        default:
            return handleUndefinedCall(callNode, CI, df);
    }

    LLVMNode *destNode = getOperand(callNode, dest, 1);
    assert(destNode && "No operand for intrinsic call");

    for (const Pointer& ptr : destNode->getPointsTo()) {
        // we could compute all the concrete offsets, but
        // these functions usually set the whole memory,
        // so if we use UNKNOWN_OFFSET, the effect is the same
        changed |= df->add(Pointer(ptr.obj, UNKNOWN_OFFSET), callNode);
    }

    return changed;
}

bool LLVMReachingDefsAnalysis::handleUndefinedCall(LLVMNode *callNode,
                                                   DefMap *df)
{
    CallInst *CI = cast<CallInst>(callNode->getKey());
    Function *func
        = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts());

    if (func && func->isIntrinsic())
        return handleIntrinsicCall(callNode, CI, df);

    return handleUndefinedCall(callNode, CI, df);
}

bool LLVMReachingDefsAnalysis::handleCallInst(LLVMDependenceGraph *graph,
                                              LLVMNode *callNode, DefMap *df)
{
    bool changed = false;

    LLVMNode *exitNode = graph->getExit();
    // the function doesn't return?
    if (!exitNode)
        return false;

    DefMap *subgraph_df = getDefMap(exitNode);
    // now handle all parameters
    // and global variables that are as parameters
    changed |= handleParams(callNode, graph, df, subgraph_df);

    return changed;
}

bool LLVMReachingDefsAnalysis::handleCallInst(LLVMNode *callNode, DefMap *df)
{
    bool changed = false;

    if (!callNode->hasSubgraphs())
        return handleUndefinedCall(callNode, df);

    for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs())
        changed |= handleCallInst(subgraph, callNode, df);

    return changed;
}

bool LLVMReachingDefsAnalysis::handleStoreInst(LLVMNode *storeNode, DefMap *df,
                                               PointsToSetT *&strong_update)
{
    bool changed = false;
    llvm::StoreInst *SI = cast<StoreInst>(storeNode->getValue());
    LLVMNode *ptrNode = getOperand(storeNode, SI->getPointerOperand(), 0);
    assert(ptrNode && "No pointer operand");

    // update definitions
    PointsToSetT& S = ptrNode->getPointsTo();
    // if we have only one concrete pointer (known pointer
    // with known offset), it is safe to do strong update
    if (S.size() == 1) {
        const Pointer& ptr = *S.begin();
        // NOTE: we don't have good mechanism to diferentiate
        // heap-allocated objects yet, so if the pointer points to heap,
        // we must do weak update
        if (ptr.isKnown() && !ptr.offset.isUnknown() && !ptr.pointsToHeap()) {
            changed |= df->update(ptr, storeNode);
            strong_update = &S;
            return changed;
        }

        // else fall-through to weak update
    }

    // weak update
    for (const Pointer& ptr : ptrNode->getPointsTo())
        changed |= df->add(ptr, storeNode);

    return changed;
}

bool LLVMReachingDefsAnalysis::runOnNode(LLVMNode *node, LLVMNode *pred)
{
    bool changed = false;
    // pointers that should not be updated
    // because they were updated strongly
    PointsToSetT *strong_update = nullptr;

    // update states according to predecessors
    DefMap *df = getDefMap(node);
    if (pred) {
        const Value *predVal = pred->getKey();
        // if the predecessor is StoreInst, it add and may kill some definitions
        if (isa<StoreInst>(predVal))
            changed |= handleStoreInst(pred, df, strong_update);
        // call inst may add some definitions to (StoreInst in subgraph)
        else if (isa<CallInst>(predVal))
            changed |= handleCallInst(pred, df);

        DefMap *pred_df = getDefMap(pred);
        changed |= df->merge(pred_df, strong_update);
        // either we have nothing to merge or we merged something
        // if predecessor has something
        assert(pred_df->empty() || !df->empty());
    } else { // BB predecessors
        LLVMBBlock *BB = node->getBBlock();
        assert(BB && "Node has no BB");

        for (LLVMBBlock *predBB : BB->predecessors()) {
            pred = predBB->getLastNode();
            assert(pred && "BB has no last node");

            const Value *predVal = pred->getKey();

            if (isa<StoreInst>(predVal))
                changed |= handleStoreInst(pred, df, strong_update);
            else if (isa<CallInst>(predVal))
                changed |= handleCallInst(pred, df);

            DefMap *pred_df = getDefMap(pred);
            changed |= df->merge(pred_df, nullptr);
            assert(pred_df->empty() || !df->empty());
        }
    }

    return changed;
}

} // namespace analysis
} // namespace dg
