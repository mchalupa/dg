#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"
#include "DefUse.h"
#include "AnalysisGeneric.h"

#include "analysis/DFS.h"
#include "llvm-debug.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg)
{
    Module *m = dg->getModule();
    // set data layout
    DL = m->getDataLayout();
}

Pointer LLVMDefUseAnalysis::getConstantExprPointer(const ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

DefMap::DefMap(const DefMap& o)
{
    merge(&o);
}

bool DefMap::merge(const DefMap *oth, PointsToSetT *without)
{
    bool changed = false;

    if (this == oth)
        return false;

    for (auto it : oth->defs) {
        const Pointer& ptr = it.first;

        // should we skip this pointer
        if (without && without->count(ptr) != 0)
            continue;

        // our values that we have for
        // this pointer
        ValuesSetT& our_vals = defs[ptr];

        // copy values that have map oth for the
        // pointer to our values
        for (LLVMNode *defnode : it.second) {
            changed |= our_vals.insert(defnode).second;
        }
    }

    return changed;
}

bool DefMap::add(const Pointer& p, LLVMNode *n)
{
    return defs[p].insert(n).second;
}

bool DefMap::update(const Pointer& p, LLVMNode *n)
{
    bool ret;
    ValuesSetT& dfs = defs[p];

    ret = dfs.count(n) == 0 || dfs.size() > 1;
    dfs.clear();
    dfs.insert(n);

    return ret;
}

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

// if we define a global variable in the subprocedure,
// add it as a parameter, so that we'll propagate the
// definition as a parameter
static void addGlobalsAsParameters(LLVMDependenceGraph *graph,
                                   LLVMNode *callNode, DefMap *subgraph_df)
{
    // if some global variable is defined in the subprocedure,
    // we must propagate it to the caller
    for (auto it : *subgraph_df) {
        const Pointer& ptr = it.first;

        if (!ptr.isKnown())
            continue;

        if (isa<GlobalVariable>(ptr.obj->node->getKey())) {
            // add actual params
            LLVMDGParameters *params = callNode->getParameters();
            if (!params) {
                params = new LLVMDGParameters();
                callNode->setParameters(params);
            }

            const Value *val = ptr.obj->node->getKey();
            if (params->findGlobal(val))
                continue;

            // FIXME we don't need this one
            LLVMNode *pact = new LLVMNode(val);
            params->addGlobal(val, pact);
            callNode->addControlDependence(pact);

            // add formal parameters
            params = graph->getParameters();
            if (!params) {
                params = new LLVMDGParameters();
                graph->setParameters(params);
            }

            LLVMNode *pform = new LLVMNode(val);
            params->addGlobal(val, pform);
            LLVMNode *entry = graph->getEntry();
            entry->addControlDependence(pform);

            // global param is only the output param,
            // so the arrow goes from formal to actual
            pform->addDataDependence(pact);
        }
    }
}

static bool handleGlobals(LLVMNode *callNode, DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;

    // get actual parameters (operands) and for every pointer in there
    // check if the memory location it points to gets defined
    // in the subprocedure
    LLVMDGParameters *params = callNode->getParameters();
    // if we have params, process params
    if (!params)
        return false;

    for (auto it : params->getGlobals()) {
        LLVMNode *p = it.second;
        // the points-to is stored in the real global node
        LLVMNode *global = callNode->getDG()->getNode(p->getKey());
        assert(global && "Do not have a global node");

        for (const Pointer& ptr : global->getPointsTo()) {
            ValuesSetT& defs = subgraph_df->get(ptr);
            if (defs.empty())
                continue;

            changed |= df->add(ptr, p);
        }
    }

    return changed;
}



static bool handleParams(LLVMNode *callNode, DefMap *df, DefMap *subgraph_df)
{
    bool changed = false;

    // get actual parameters (operands) and for every pointer in there
    // check if the memory location it points to gets defined
    // in the subprocedure
    LLVMDGParameters *params = callNode->getParameters();
    // if we have params, process params
    if (!params)
        return false;

    // operand[0] is the called func
    for (int i = 1, e = callNode->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = callNode->getOperand(i);
        if (!op)
            continue;

        if (!op->isPointerTy())
            continue;

        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
            DBG("ERR: no actual param for " << *op->getKey());
            continue;
        }

        for (const Pointer& ptr : op->getPointsTo()) {
            ValuesSetT& defs = subgraph_df->get(ptr);
            if (defs.empty())
                continue;

            changed |= df->add(ptr, p->out);
        }
    }

    return changed;
}

static bool handleCallInst(LLVMDependenceGraph *graph,
                           LLVMNode *callNode, DefMap *df)
{
    bool changed = false;

    LLVMNode *exitNode = graph->getExit();
    assert(exitNode && "No exit node in subgraph");

    DefMap *subgraph_df = getDefMap(exitNode);
    // first add global as a parameters
    addGlobalsAsParameters(graph, callNode, subgraph_df);
    // now handle all parameters
    // and global variables that are as parameters
    changed |= handleParams(callNode, df, subgraph_df);
    changed |= handleGlobals(callNode, df, subgraph_df);

    return changed;
}

static bool handleCallInst(LLVMNode *callNode, DefMap *df)
{
    bool changed = false;

    for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs())
        changed |= handleCallInst(subgraph, callNode, df);

    return changed;
}

static bool handleStoreInst(LLVMNode *storeNode, DefMap *df,
                            PointsToSetT *&strong_update)
{
    bool changed = false;
    LLVMNode *ptrNode = storeNode->getOperand(0);
    assert(ptrNode && "No pointer operand");

    // update definitions
    PointsToSetT& S = ptrNode->getPointsTo();
    if (S.size() == 1) {// strong update
        changed |= df->update(*S.begin(), storeNode);
        strong_update = &S;
    } else { // weak update
        for (const Pointer& ptr : ptrNode->getPointsTo())
            changed |= df->add(ptr, storeNode);
    }

    return changed;
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node)
{
    bool changed = false;
    // pointers that should not be updated
    // because they were updated strongly
    PointsToSetT *strong_update = nullptr;

    // update states according to predcessors
    DefMap *df = getDefMap(node);
    LLVMNode *pred = node->getPredcessor();
    if (pred) {
        const Value *predVal = pred->getKey();
        // if the predcessor is StoreInst, it add and may kill some definitions
        if (isa<StoreInst>(predVal))
            changed |= dg::analysis::handleStoreInst(pred, df, strong_update);
        // call inst may add some definitions to (StoreInst in subgraph)
        else if (isa<CallInst>(predVal))
            changed |= dg::analysis::handleCallInst(pred, df);

        changed |= df->merge(getDefMap(pred), strong_update);
    } else { // BB predcessors
        LLVMBBlock *BB = node->getBasicBlock();
        assert(BB && "Node has no BB");

        for (auto predBB : BB->predcessors()) {
            pred = predBB->getLastNode();
            assert(pred && "BB has no last node");

            const Value *predVal = pred->getKey();

            if (isa<StoreInst>(predVal))
                changed |= dg::analysis::handleStoreInst(pred, df, strong_update);
            else if (isa<CallInst>(predVal))
                changed |= dg::analysis::handleCallInst(pred, df);

            df->merge(getDefMap(pred), nullptr);
        }
    }

    return changed;
}

} // namespace analysis
} // namespace dg


/// --------------------------------------------------
//   Add def-use edges
/// --------------------------------------------------
namespace dg {
namespace analysis {

LLVMNode *LLVMDefUseAnalysis::getOperand(LLVMNode *node,
                                        const Value *val, unsigned int idx)
{
    return dg::analysis::getOperand(node, val, idx, DL);
}

static void addIndirectDefUsePtr(const Pointer& ptr, LLVMNode *to, DefMap *df)
{
    if (!ptr.isKnown()) {
        DBG("ERR: pointer pointing to unknown location, UNSOUND! "
               << *to->getKey());
        return;
    }

    LLVMNode *ptrnode = ptr.obj->node;
    const Value *ptrVal = ptrnode->getKey();
    // functions does not have indirect reaching definitions
    if (isa<Function>(ptrVal))
        return;

    ValuesSetT& defs = df->get(ptr);
    // do we have any reaching definition at all?
    if (defs.empty()) {
        // we do not add initial def to global variables because not all
        // global variables could be used in the code and we'd redundantly
        // iterate through the defintions. Do it lazily here.
        if (isa<GlobalVariable>(ptrVal)) {
                // ok, so the GV was defined in initialization phase,
                // so the reaching definition for the ptr is there.
                // If it was not defined, then we still want the edge
                // from the global node in this case
                defs.insert(ptrnode);
        } else if (isa<AllocaInst>(ptrVal)) {
            // AllocaInst without any reaching definition
            // may mean that the value is undefined. Nevertheless
            // we use the value that is defined via the AllocaInst,
            // so add definition on the AllocaInst
            // This is the same as with global variables
            defs.insert(ptrnode);
        } else if (isa<ConstantPointerNull>(ptrVal)) {
            // just do nothing, it has no reaching definition
            return;
        } else {
            DBG("WARN: no reaching definition for " << *ptr.obj->node->getKey()
                << " + " << *ptr.offset);
            return;
        }
    }

    assert(!defs.empty());
    for (LLVMNode *n : defs)
        n->addDataDependence(to);

    // if we got pointer to our object with UNKNOWN_OFFSET,
    // it still can be reaching definition, so we must take it into
    // account
    ValuesSetT& defsUnknown = df->get(Pointer(ptr.obj, UNKNOWN_OFFSET));
    if (!defsUnknown.empty()) {
        for (LLVMNode *n : defsUnknown)
            n->addDataDependence(to);
    }
}

static void addIndirectDefUse(LLVMNode *ptrNode, LLVMNode *to, DefMap *df)
{
    // iterate over all memory locations that this
    // store can define and check where they are defined
    for (const Pointer& ptr : ptrNode->getPointsTo())
        addIndirectDefUsePtr(ptr, to, df);
}

// return Value used on operand LLVMNode
// It is either the operand itself or
// global value used in ConstantExpr if the
// operand is ConstantExpr
static void addStoreLoadInstDefUse(LLVMNode *storeNode, LLVMNode *op, DefMap *df)
{
    const Value *val = op->getKey();
    if (isa<ConstantExpr>(val)) {
        // it should be one ptr
        PointsToSetT& PS = op->getPointsTo();
        assert(PS.size() == 1);

        const Pointer& ptr = *PS.begin();
        addIndirectDefUsePtr(ptr, storeNode,  df);
    } else {
        op->addDataDependence(storeNode);
    }
}

void LLVMDefUseAnalysis::handleStoreInst(const StoreInst *Inst, LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *valNode = node->getOperand(1);

    // this node uses what is defined on valNode
    if (valNode) {
        addStoreLoadInstDefUse(node, valNode, df);
    }
#ifdef DEBUG_ENABLED
    else {
        if (!isa<ConstantInt>(Inst->getValueOperand()))
            DBG("ERR def-use: Unhandled value operand for " << *Inst);
    }
#endif

    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode);

    // and also uses what is defined on ptrNode
    addStoreLoadInstDefUse(node, ptrNode, df);
}

void LLVMDefUseAnalysis::handleLoadInst(const llvm::LoadInst *Inst, LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *ptrNode = getOperand(node, Inst->getPointerOperand(), 0);
    assert(ptrNode && "No ptr node");

    // load inst is reading from the memory,
    // so add indirect def-use edges
    addIndirectDefUse(ptrNode, node, df);

    // load inst is reading from the memory that is pointed by
    // the top-level value, so add def-use edge
    addStoreLoadInstDefUse(node, ptrNode, df);
}


static void addOutParamsEdges(LLVMDependenceGraph *graph)
{
    LLVMNode *exitNode = graph->getExit();
    assert(exitNode && "No exit node in subgraph");
    DefMap *df = getDefMap(exitNode);

    // add edges between formal params and the output params
    LLVMDGParameters *params = graph->getParameters();
    if (params) {
        for (auto it : *params) {
            const Value *val = it.first;
            if (!val->getType()->isPointerTy())
                continue;

            LLVMDGParameter& p = it.second;

            // points to set is contained in the input param
            for (const Pointer& ptr : p.in->getPointsTo()) {
                ValuesSetT& defs = df->get(ptr);
                if (defs.empty())
                    continue;

                // ok, the memory location is defined in this subgraph,
                // so add data dependence edge to the out param
                for (LLVMNode *def : defs)
                    def->addDataDependence(p.out);
            }
        }

        // add edges between used globals and corresponding global's parameter
        for (auto it : params->getGlobals()) {
            LLVMNode *p = it.second;

            // points-to of globals is stored in the global itself
            LLVMNode *g = graph->getNode(p->getKey());
            assert(g && "Do not have a global node");

            for (const Pointer& ptr : g->getPointsTo()) {
                ValuesSetT& defs = df->get(ptr);
                if (defs.empty())
                    continue;

                // ok, the memory location is defined in this subgraph,
                // so add data dependence edge to the global
                for (LLVMNode *def : defs)
                    def->addDataDependence(p);
            }
        }
    }

}

static void addOutParamsEdges(LLVMNode *callNode)
{
    for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs()) {
        addOutParamsEdges(subgraph);

        // FIXME we're loosing some accuracy here and
        // this edges causes that we'll go into subprocedure
        // even with summary edges
        if (!callNode->isVoidTy())
            subgraph->getExit()->addDataDependence(callNode);
    }
}

static void handleCallInst(LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMDGParameters *params = node->getParameters();

    // if we have a node for the called function,
    // it is call via function pointer, so add the
    // data dependence edge to corresponding node
    if (!isa<Function>(node->getKey())) {
        LLVMNode *n = node->getOperand(0);
        if (n)
            n->addDataDependence(node);
    }

    if (!params) // function has no arguments
        return;

    // add def-use edges between parameters and the operands
    // parameters begin from 1
    for (int i = 1, e = node->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = node->getOperand(i);
        if (!op)
            continue;

        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
            DBG("ERR: no actual param for " << *op->getKey());
            continue;
        }

        if (op->isPointerTy()) {
            // add data dependencies to in parameters
            addIndirectDefUse(op, p->in, df);
            // fall-through to
            // add also top-level def-use edge
        }

        op->addDataDependence(p->in);
    }

    addOutParamsEdges(node);
}

static void handleInstruction(const Instruction *Inst, LLVMNode *node)
{
    LLVMDependenceGraph *dg = node->getDG();

    for (auto I = Inst->op_begin(), E = Inst->op_end(); I != E; ++I) {
        LLVMNode *op = dg->getNode(*I);
        if (op)
            op->addDataDependence(node);
/* this is hit with switch
#ifdef DEBUG_ENABLED
        else if (!isa<ConstantInt>(*I) && !isa<BranchInst>(Inst))
            DBG("WARN: no node for operand " << **I
                   << "in " << *Inst);
#endif
*/
    }
}

void LLVMDefUseAnalysis::handleNode(LLVMNode *node)
{
    const Value *val = node->getKey();

    if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        handleStoreInst(Inst, node);
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        handleLoadInst(Inst, node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    } else if (const Instruction *Inst = dyn_cast<Instruction>(val)) {
        handleInstruction(Inst, node); // handle rest of Insts
    } else {
        DBG("ERR: Unhandled instruction " << *val);
    }
}

void handleBlock(LLVMBBlock *BB, LLVMDefUseAnalysis *analysis)
{
    LLVMNode *n = BB->getFirstNode();
    while (n) {
        analysis->handleNode(n);
        n = n->getSuccessor();
    }
}

void LLVMDefUseAnalysis::addDefUseEdges()
{
    // it doesn't matter how we'll go through the nodes
    BBlockDFS<LLVMNode> runner(DFS_INTERPROCEDURAL | DFS_BB_CFG);
    runner.run(dg->getEntryBB(), handleBlock, this);
}

} // namespace analysis
} // namespace dg
