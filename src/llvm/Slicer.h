#ifndef _LLVM_DG_SLICER_H_
#define _LLVM_DG_SLICER_H_

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include "analysis/Slicing.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

namespace dg {

class LLVMNode;

class LLVMSlicer : public analysis::Slicer<LLVMNode>
{
public:
    LLVMSlicer() :nodesTotal(0), nodesRemoved(0) {}

    /* virtual */
    void removeNode(LLVMNode *node)
    {
        using namespace llvm;

        Value *val = const_cast<Value *>(node->getKey());
        val->replaceAllUsesWith(UndefValue::get(val->getType()));

        Instruction *Inst = dyn_cast<Instruction>(val);
        if (Inst) {
            // if there are any other uses of this value,
            // just replace them with undef
            Inst->eraseFromParent();
        } else {
            GlobalVariable *GV = dyn_cast<GlobalVariable>(val);
            if (GV)
                GV->eraseFromParent();
        }
    }

    // override slice method
    uint32_t slice(LLVMNode *start, uint32_t sl_id = 0)
    {
        (void) sl_id;
        (void) start;

        assert(0 && "Do not use this method with LLVM dg");
        return 0;
    }

    uint32_t slice(LLVMDependenceGraph *maindg,
                   LLVMNode *start, uint32_t sl_id = 0)
    {
        // mark nodes for slicing
        assert(start || sl_id != 0);
        if (start)
            sl_id = mark(start, sl_id);

        // take every subgraph and slice it intraprocedurally
        for (auto it : maindg->getSubgraphs()) {
            LLVMDependenceGraph *subdg = it.second;
            sliceGraph(subdg, sl_id);
        }

        // slice main dg
        sliceGraph(maindg, sl_id);

        return sl_id;
    }

    std::pair<uint64_t, uint64_t> getStatistics() const
    {
        return std::pair<uint64_t, uint64_t>(nodesTotal, nodesRemoved);
    }

private:
    void sliceCallNode(LLVMNode *callNode,
                       LLVMDependenceGraph *graph, uint32_t slice_id)
    {
        LLVMDGParameters *actualparams = callNode->getParameters();
        LLVMDGParameters *formalparams = graph->getParameters();

        if (!actualparams) {
            assert(!formalparams && "Have only one of params");
            return; // no params - nothing to do
        }

        assert(formalparams && "Have only one of params");
        assert(formalparams->size() == actualparams->size());

        // FIXME slice arguments away
    }

    void sliceCallNode(LLVMNode *callNode, uint32_t slice_id)
    {
        for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs())
            sliceCallNode(callNode, subgraph, slice_id);
    }

    void sliceGraph(LLVMDependenceGraph *dg, uint32_t slice_id)
    {
            for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
                LLVMNode *n = I->second;

                // we added this node artificially and
                // we don't want to slice it away or
                // take any other action on it
                if (n == dg->getExit())
                    continue;

                ++nodesTotal;

                if (llvm::isa<llvm::CallInst>(n->getKey()))
                    sliceCallNode(n, slice_id);

                if (n->getSlice() != slice_id) {
                    removeNode(n);
                    dg->deleteNode(n);
                    ++nodesRemoved;
                }
            }

            if (dg->ownsGlobalNodes()) {
                auto nodes = dg->getGlobalNodes();
                for (auto I = nodes->begin(), E = nodes->end(); I != E; ++I) {
                    LLVMNode *n = I->second;
                    ++nodesTotal;

                    if (n->getSlice() != slice_id) {
                        removeNode(n);
                        dg->deleteGlobalNode(I);
                        ++nodesRemoved;
                    }
                }
            }
    }

    uint64_t nodesTotal;
    uint64_t nodesRemoved;
};

} // namespace dg

#endif  // _LLVM_DG_SLICER_H_

