#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"
#include "DefUse.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL)
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

static bool handleStoreInst(const StoreInst *Inst, LLVMNode *node,
                            PointsToSetT *&strong_update)
{
    bool changed = false;
    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode && "No pointer operand");

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

    /*
    if (const AllocaInst *Inst = dyn_cast<AllocaInst>(val)) {
        changed |= handleAllocaInst(Inst, node);
    } 
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        changed |= handleLoadInst(Inst, node);
    } else if (const GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        changed |= handleGepInst(Inst, node);
    } else if (const CallInst *Inst = dyn_cast<CallInst>(val)) {
        changed |= handleCallInst(Inst, node);
    } else if (const ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        changed |= handleReturnInst(Inst, node);
    } else if (const BitCastInst *Inst = dyn_cast<BitCastInst>(val)) {
        changed |= handleBitCastInst(Inst, node);
    } else {
        const Instruction *I = dyn_cast<Instruction>(val);
        assert(I && "Not an Instruction?");

        if (I->mayReadOrWriteMemory())
            errs() << "WARN: Unhandled instruction: " << *val << "\n";
    }
    */

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
