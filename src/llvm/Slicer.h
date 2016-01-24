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
    LLVMSlicer() :nodesTotal(0), nodesRemoved(0), blocksRemoved(0) {}

    void keepFunctionUntouched(const char *n)
    {
        dont_touch.insert(n);
    }

    /* virtual */
    bool removeNode(LLVMNode *node)
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

        return true;
    }

    /* virtual */
    void removeBlock(LLVMBBlock *block)
    {
        assert(block);

        const llvm::Value *val = block->getKey();
        if (val == nullptr)
            return;

        llvm::Value *llvmval = const_cast<llvm::Value *>(val);
        llvm::BasicBlock *blk = llvm::cast<llvm::BasicBlock>(llvmval);

        //llvm::errs() << "Deleting: " << *blk << "\n===\n";

        LLVMDependenceGraph *dg = block->getDG();
        dg->getConstructedBlocks().erase(blk);

        blk->eraseFromParent();
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

    static inline bool shouldSliceInst(const llvm::Value *val)
    {
        using namespace llvm;
        const Instruction *Inst = dyn_cast<Instruction>(val);
        if (!Inst)
            return true;

        switch (Inst->getOpcode()) {
#if 0
            case Instruction::Br:
            case Instruction::Switch:
#endif
            case Instruction::Ret:
            case Instruction::Unreachable:
                return false;
            default:
                return true;
        }
    }

/*
    static LLVMBBlock *
    createNewExitBB(LLVMDependenceGraph *graph)
    {
        using namespace llvm;

        LLVMBBlock *exitBB = new LLVMBBlock();

        Module *M = graph->getModule();
        LLVMContext& Ctx = M->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "safe_exit");
        // terminate the basic block with unreachable
        // (we'll call _exit() before that)
        UnreachableInst *UI = new UnreachableInst(Ctx, block);
        Type *size_t_Ty;

        // call the _exit function
        Constant *Func = M->getOrInsertFunction("_exit",
                                                Type::getVoidTy(Ctx),
                                                Type::getInt32Ty(Ctx),
                                                NULL);

        std::vector<Value *> args;
        args.push_back(ConstantInt::get(Type::getInt32Ty(Ctx), 0));
        CallInst *CI = CallInst::Create(Func, args);
        CI->insertBefore(UI);

        Value *fval = const_cast<Value *>(graph->getEntry()->getKey());
        Function *F = cast<Function>(fval);
        F->getBasicBlockList().push_back(block);

        exitBB->append(new LLVMNode(CI));
        exitBB->append(new LLVMNode(UI));
        exitBB->setKey(block);
        exitBB->setDG(graph);

        return exitBB;
    }
*/

    static LLVMBBlock *
    createNewExitBB(LLVMDependenceGraph *graph)
    {
        using namespace llvm;

        LLVMBBlock *exitBB = new LLVMBBlock();

        Module *M = graph->getModule();
        LLVMContext& Ctx = M->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "safe_return");

        Value *fval = const_cast<Value *>(graph->getEntry()->getKey());
        Function *F = cast<Function>(fval);
        F->getBasicBlockList().push_back(block);

        // fill in basic block just with return value
        ReturnInst *RI;
        if (F->getReturnType()->isVoidTy())
            RI = ReturnInst::Create(Ctx, block);
        else
            RI = ReturnInst::Create(Ctx, UndefValue::get(F->getReturnType()), block);

        exitBB->append(new LLVMNode(RI));
        exitBB->setKey(block);
        exitBB->setDG(graph);

        return exitBB;
    }

    // when we sliced away a branch of CFG, we need to reconnect it
    // to exit block, since on this path we would silently terminate
    // (this path won't have any effect on the property anymore)
    void makeGraphComplete(LLVMDependenceGraph *graph)
    {
        LLVMBBlock *oldExitBB = graph->getExitBB();
        assert(oldExitBB && "Don't have exit BB");

        LLVMBBlock *newExitBB = nullptr;

        for (auto it : graph->getConstructedBlocks()) {
            const llvm::BasicBlock *llvmBB = it.first;
            const llvm::TerminatorInst *tinst = llvmBB->getTerminator();
            LLVMBBlock *BB = it.second;

            DGContainer<uint8_t> labels;
            for (auto succ : BB->successors()) {
                // skip artificial return basic block
                if (succ.label == 255)
                    continue;

                // we have normal (not 255) label to exit node?
                // replace it with call to exit, because that means
                // that some path, that normally returns, was sliced
                // away and so if we're on this path, we won't affect
                // behaviour of slice - we can exit
                if (succ.target == oldExitBB) {
                    if (!newExitBB) {
                        newExitBB = createNewExitBB(graph);
                        oldExitBB->remove();
                        graph->setExitBB(newExitBB);
                        graph->setExit(newExitBB->getLastNode());
                    }

                    succ.target = newExitBB;
                } else
                    labels.insert(succ.label);
            }

            // replace missing labels
            for (uint8_t i = 0; i < tinst->getNumSuccessors(); ++i) {
                if (labels.contains(i))
                    continue;
                else {
                    /*
                    llvm::errs() << "adding new succ "
                                 << *newExitBB->getKey() << "\n"
                                 << (int) i << " to " << *llvmBB << "\n";
                                 */
                     if (!newExitBB) {
                        newExitBB = createNewExitBB(graph);
                        oldExitBB->remove();
                        graph->setExitBB(newExitBB);
                        graph->setExit(newExitBB->getLastNode());
                    }

                    bool ret = BB->addSuccessor(newExitBB, i);
                    assert(ret);
                }
            }
        }
    }

    // remove BBlocks that contain no node that should be in
    // sliced graph. Overrides parents method
    void sliceBBlocks(LLVMDependenceGraph *graph, uint32_t sl_id)
    {
        auto& CB = graph->getConstructedBlocks();
#ifdef DEBUG_ENABLED
        uint32_t blocksNum = CB.size();
#endif
        // gather the blocks
        // FIXME: we don't need two loops, just go carefully
        // through the constructed blocks (keep temporary always-valid iterator)
        std::set<LLVMBBlock *> blocks;
        for (auto it : CB) {
            if (it.second->getSlice() != sl_id)
                blocks.insert(it.second);
        }

        for (LLVMBBlock *blk : blocks) {
            // call specific handlers
            removeBlock(blk);
            blk->remove();
            nodesRemoved += blk->size();
            nodesTotal += blk->size();
            ++blocksRemoved;
        }

#ifdef DEBUG_ENABLED
        assert(CB.size() + blocks.size() == blocksNum &&
                "Inconsistency in sliced blocks");
#endif
    }

    void sliceGraph(LLVMDependenceGraph *graph, uint32_t slice_id)
    {
        // first slice away bblocks that should go away
        sliceBBlocks(graph, slice_id);

        // make graph complete
        makeGraphComplete(graph);

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

        // create new CFG edges between blocks after slicing
        reconnectLLLVMBasicBlocks(graph);
    }

    bool dontTouch(const llvm::StringRef& r)
    {
        for (const char *n : dont_touch)
            if (r.equals(n))
                return true;

        return false;
    }

    void reconnectBBlock(LLVMBBlock *BB, llvm::BasicBlock *llvmBB)
    {
        llvm::TerminatorInst *tinst = llvmBB->getTerminator();
        if (!tinst) {
            // block has no terminator - that is unreachable. It may occur for example
            // if we have:
            //   call error()
            //   br %exit
            //
            //  The br instruction has no meaning when error() abort,
            //  but if error is not marked as noreturn, then the br
            //  will be there and will get sliced, making the block
            //  unterminated
            new llvm::UnreachableInst(llvmBB->getContext(), llvmBB);
            // and that is all we can do here
            return;
        }

        assert((BB->successorsNum() <= 2 || llvm::isa<llvm::SwitchInst>(tinst))
                && "BB has more than two successors (and it's not a switch)");

        for (const LLVMBBlock::BBlockEdge& succ : BB->successors()) {
            // skip artificial return basic block
            if (succ.label == 255)
                continue;

            llvm::Value *val = const_cast<llvm::Value *>(succ.target->getKey());
            assert(val && "nullptr as BB's key");
            llvm::BasicBlock *llvmSucc = llvm::cast<llvm::BasicBlock>(val);
            tinst->setSuccessor(succ.label, llvmSucc);
        }

        // if the block still does not have terminator
    }

    void reconnectLLLVMBasicBlocks(LLVMDependenceGraph *graph)
    {
        for (auto it : graph->getConstructedBlocks()) {
            llvm::BasicBlock *llvmBB = const_cast<llvm::BasicBlock *>(it.first);
            LLVMBBlock *BB = it.second;

            reconnectBBlock(BB, llvmBB);
        }
    }

    uint64_t nodesTotal;
    uint64_t nodesRemoved;
    uint32_t blocksRemoved;

    // do not slice these functions at all
    std::set<const char *> dont_touch;
};
} // namespace dg

#endif  // _LLVM_DG_SLICER_H_

