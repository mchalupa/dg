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

    void keepFunctionUntouched(const char *n)
    {
        dont_touch.insert(n);
    }

    /* virtual */
    void removeNode(LLVMNode *node)
    {
        using namespace llvm;

        Value *val = const_cast<Value *>(node->getKey());
        // if there are any other uses of this value,
        // just replace them with undef
        val->replaceAllUsesWith(UndefValue::get(val->getType()));

        Instruction *Inst = dyn_cast<Instruction>(val);
        if (Inst) {
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
        // this includes the main graph
        extern std::map<const llvm::Value *,
                        LLVMDependenceGraph *> constructedFunctions;
        for (auto it : constructedFunctions) {
            if (dontTouch(it.first->getName()))
                continue;

            LLVMDependenceGraph *subdg = it.second;
            sliceGraph(subdg, sl_id);
        }

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

        /*
        if (!actualparams) {
            assert(!formalparams && "Have only one of params");
            return; // no params - nothing to do
        }

        assert(formalparams && "Have only one of params");
        assert(formalparams->size() == actualparams->size());
        */

        // FIXME slice arguments away
    }

    void sliceCallNode(LLVMNode *callNode, uint32_t slice_id)
    {
        for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs())
            sliceCallNode(callNode, subgraph, slice_id);
    }

    static bool shouldSliceInst(const llvm::Value *val)
    {
        using namespace llvm;
        const Instruction *Inst = dyn_cast<Instruction>(val);
        if (!Inst)
            return true;

        switch (Inst->getOpcode()) {
            case Instruction::Ret:
            case Instruction::Unreachable:
            /*
            case Instruction::Br:
            case Instruction::Switch:
            */
                return false;
            default:
                return true;
        }
    }

    void sliceGraph(LLVMDependenceGraph *graph, uint32_t slice_id)
    {
            // first slice away bblocks that should go away
            sliceBBlocks(graph->getEntryBB(), slice_id);

            // now slice away instructions from BBlocks that left
            for (auto I = graph->begin(), E = graph->end(); I != E;) {
                LLVMNode *n = I->second;
                // shift here, so that we won't corrupt the iterator
                // by deleteing the node
                ++I;

                // we added this node artificially and
                // we don't want to slice it away or
                // take any other action on it
                if (n == graph->getExit())
                    continue;

                ++nodesTotal;

                // keep instructions like ret or unreachable
                // FIXME: if this is ret of some value, then
                // the value is undef now, so we should
                // replace it by void ref
                if (!shouldSliceInst(n->getKey()))
                    continue;

                /*
                if (llvm::isa<llvm::CallInst>(n->getKey()))
                    sliceCallNode(n, slice_id);
                    */

                if (n->getSlice() != slice_id) {
                    removeNode(n);
                    graph->deleteNode(n);
                    ++nodesRemoved;
                }
            }

            #if 0
            if (graph->ownsGlobalNodes()) {
                auto nodes = graph->getGlobalNodes();
                for (auto I = nodes->begin(), E = nodes->end(); I != E;) {
                    LLVMNode *n = I->second;
                    ++nodesTotal;
                    ++I;

                    // do not slice away entry nodes of
                    // 'untouchable' functions
                    const llvm::Function *func
                        = llvm::dyn_cast<llvm::Function>(n->getKey());
                    if (func && dontTouch(func->getName()))
                        continue;

                    if (n->getSlice() != slice_id) {
                        removeNode(n);
                        graph->deleteGlobalNode(I);
                        ++nodesRemoved;
                    }
                }
            }
            #endif
    }

    bool dontTouch(const llvm::StringRef& r)
    {
        for (const char *n : dont_touch)
            if (r.equals(n))
                return true;

        return false;
    }

    uint64_t nodesTotal;
    uint64_t nodesRemoved;

    // do not slice these functions at all
    std::set<const char *> dont_touch;
};
} // namespace dg

#endif  // _LLVM_DG_SLICER_H_

