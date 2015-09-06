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

static bool handleCallInst(LLVMDependenceGraph *graph,
                           LLVMNode *callNode, DefMap *df)
{
    bool changed = false;

    LLVMNode *exitNode = graph->getExit();
    assert(exitNode && "No exit node in subgraph");

    DefMap *subgraph_df = getDefMap(exitNode);

    // get actual parameters (operands) and for every pointer in there
    // check if the memory location it points to get defined
    // in the subprocedure
    LLVMNode **operands = callNode->getOperands();
    LLVMDGParameters *params = callNode->getParameters();
    // we call this func only on non-void functions
    assert(params && "No actual parameters in call node");

    for (int i = 0, e = callNode->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = operands[i];
        if (!op)
            continue;

        if (!op->isPointerTy())
            continue;

        for (const Pointer& ptr : op->getPointsTo()) {
            ValuesSetT& defs = subgraph_df->get(ptr);
            if (defs.empty())
                continue;

            // ok, the memory location is defined in the subprocedure,
            // so add reaching definition to out param of this call node
            LLVMDGParameter *p = params->find(op->getKey());
            assert(p && "no actual parameter");
            changed |= df->add(ptr, p->out);
        }
    }

    return changed;
}

static bool handleCallInst(LLVMNode *callNode, DefMap *df)
{
    bool changed = false;
    // ignore callInst with void prototype
    if (callNode->getOperandsNum() == 1)
        return false;

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

static void addIndirectDefUsePtr(const Pointer& ptr, LLVMNode *to, DefMap *df)
{
    ValuesSetT& defs = df->get(ptr);
    // do we have any reaching definition at all?
    if (defs.empty()) {
        // we do not add initial def to global variables because not all
        // global variables could be used in the code and we'd redundantly
        // iterate through the defintions. Do it lazily here.
        LLVMNode *ptrnode = ptr.obj->node;
        if (const GlobalVariable *GV
                = dyn_cast<GlobalVariable>(ptrnode->getKey())) {
            if (GV->hasInitializer()) {
                // ok, so the GV was defined in initialization phase,
                // so the reaching definition for the ptr is there
                defs.insert(ptrnode);
            }
        } else {
            errs() << "WARN: no reaching definition for "
                   << *ptr.obj->node->getKey()
                   << " + " << *ptr.offset << "\n";
            return;
        }
    }

    // we read ptrNode memory that is defined on these locations
    assert(!defs.empty());
    for (LLVMNode *n : defs)
        n->addDataDependence(to);
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
    } else {
        if (!isa<ConstantInt>(Inst->getValueOperand()))
            errs() << "ERR def-use: Unhandled value operand for "
                   << *Inst << "\n";
    }

    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode);

    // and also uses what is defined on ptrNode
    addStoreLoadInstDefUse(node, ptrNode, df);
}

void LLVMDefUseAnalysis::handleLoadInst(LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *ptrNode = node->getOperand(0);
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
    LLVMDGParameters *params = graph->getParameters();
    assert(params && "No formal params in subgraph with actual params");

    LLVMNode *exitNode = graph->getExit();
    assert(exitNode && "No exit node in subgraph");
    DefMap *df = getDefMap(exitNode);

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
    LLVMNode **operands = node->getOperands();
    LLVMDGParameters *params = node->getParameters();

    if (!params) // function has no arguments
        return;

    for (int i = 0, e = node->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = operands[i];
        if (!op)
            continue;

        // find actual parameter
        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
            errs() << "ERR: no actual parameter for " << *op->getKey() << "\n";
            continue;
        }

        if (op->isPointerTy()) {
            // add data dependencies to in parameters
            addIndirectDefUse(op, p->in, df);

            // FIXME
            // look for reaching definitions inside the procedure
            // because since this is a pointer, we can change things
        } else
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
        else if (!isa<ConstantInt>(*I) && !isa<BranchInst>(Inst))
            errs() << "WARN: no node for operand " << **I
                   << "in " << *Inst << "\n";
    }
}

void LLVMDefUseAnalysis::handleNode(LLVMNode *node)
{
    const Value *val = node->getKey();

    if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        handleStoreInst(Inst, node);
    } else if (isa<LoadInst>(val)) {
        handleLoadInst(node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    } else if (const Instruction *Inst = dyn_cast<Instruction>(val)) {
        handleInstruction(Inst, node); // handle rest of Insts
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
    BBlockDFS<LLVMNode> runner(DFS_INTERPROCEDURAL);
    runner.run(dg->getEntryBB(), handleBlock, this);
}

} // namespace analysis
} // namespace dg
