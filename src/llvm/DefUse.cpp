#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"
#include "DefUse.h"

#include "analysis/DFS.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg)
{
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

static bool handleStoreInst(const StoreInst *Inst, LLVMNode *node,
                            PointsToSetT *&strong_update)
{
    bool changed = false;
    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode && "No pointer operand");

    (void) Inst;

    // update definitions
    DefMap *df = getDefMap(node);
    PointsToSetT& S = ptrNode->getPointsTo();

    if (S.size() == 1) {// strong update
        changed |= df->update(*S.begin(), node);
        strong_update = &S;
    } else { // weak update
        for (const Pointer& ptr : ptrNode->getPointsTo())
            changed |= df->add(ptr, node);
    }

    return changed;
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node)
{
    bool changed = false;
    const Value *val = node->getKey();
    // pointers that should not be updated
    // because they were updated strongly
    PointsToSetT *strong_update = nullptr;

    if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        changed |= handleStoreInst(Inst, node, strong_update);
    }

    // update states according to predcessors
    DefMap *df = getDefMap(node);
    LLVMNode *pred = node->getPredcessor();
    if (pred) {
        changed |= df->merge(getDefMap(pred), strong_update);
    } else { // BB predcessors
        LLVMBBlock *BB = node->getBasicBlock();
        assert(BB && "Node has no BB");

        for (auto predBB : BB->predcessors()) {
            pred = predBB->getLastNode();
            assert(pred && "BB has no last node");

            df->merge(getDefMap(pred), strong_update);
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

static void handleStoreInst(LLVMNode *node)
{
    // we have only top-level dependencies here

    LLVMNode *valNode = node->getOperand(1);
    // this node uses what is defined on valNode
    if (valNode)
        valNode->addDataDependence(node);

    // and also uses what is defined on ptrNode
    LLVMNode *ptrNode = node->getOperand(0);
    ptrNode->addDataDependence(node);
}

static void addIndirectDefUse(LLVMNode *ptrNode, LLVMNode *to, DefMap *df)
{
    // iterate over all memory locations that this
    // store can define and check where they are defined
    for (const Pointer& ptr : ptrNode->getPointsTo()) {
        const ValuesSetT& defs = df->get(ptr);
        // do we have any reaching definition at all?
        if (defs.empty()) {
            const Value *val = ptrNode->getKey();
            // we do not add def to global variables, so do it here
            if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(val)) {
            //    if (GV->hasInitializer())
            } else if (isa<AllocaInst>(val)) {
                // we have the edges, this is just to suppress the warning
            } else
                errs() << "WARN: no reaching definition for " << *val << "\n";

            continue;
        }

        // we read ptrNode memory that is defined on these locations
        for (LLVMNode *n : defs)
            n->addDataDependence(to);
    }
}

static void handleLoadInst(LLVMNode *node)
{
    LLVMNode *ptrNode = node->getOperand(0);
    if (!ptrNode) {
        errs() << "ERR: No ptrNode: " << *node->getKey() << "\n";
        return;
    }

    // we use the top-level value that is defined
    // on ptrNode
     ptrNode->addDataDependence(node);

    DefMap *df = getDefMap(node);
    addIndirectDefUse(ptrNode, node, df);
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

    // if the called function returns a value,
    // make this node data dependent on that
    if (!node->isVoidTy()) {
        for (auto sub : node->getSubgraphs())
            sub->getExit()->addDataDependence(node);
    }
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

static void handleNode(LLVMNode *node)
{
    const Value *val = node->getKey();

    if (isa<StoreInst>(val)) {
        handleStoreInst(node);
    } else if (isa<LoadInst>(val)) {
        handleLoadInst(node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    } else if (const Instruction *Inst = dyn_cast<Instruction>(val)) {
        handleInstruction(Inst, node); // handle rest of Insts
    }
}

static void handleBlock(LLVMBBlock *BB, void *data)
{
    (void) data;

    LLVMNode *n = BB->getFirstNode();
    while (n) {
        handleNode(n);
        n = n->getSuccessor();
    }
}

void LLVMDefUseAnalysis::addDefUseEdges()
{
    // it doesn't matter how we'll go through the nodes
    BBlockDFS<LLVMNode> runner(DFS_INTERPROCEDURAL);
    runner.run(dg->getEntryBB(), handleBlock, nullptr);
}

} // namespace analysis
} // namespace dg
