#ifndef LLVM_DG_FAST_SLICER_H_
#define LLVM_DG_FAST_SLICER_H_

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/Config/llvm-config.h>
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/Support/CFG.h>
#else
#include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/Slicing.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/LLVMSlicer.h" // dropAllUses

namespace dg {
namespace llvmdg {

class LLVMFastSlicer {
    llvm::Module &module;
  public:

    LLVMFastSlicer(llvm::Module &m) : module(m) {}

    void keepFunctionUntouched(const char *n) {
        llvm::errs() << "Keep untouched not working " << n << "\n";
        dont_touch.insert(n);
    }

    void removeValue(llvm::Value *val) {
        using namespace llvm;
        // if there are any other uses of this value,
        // just replace them with undef
        val->replaceAllUsesWith(UndefValue::get(val->getType()));

        Instruction *I = dyn_cast<Instruction>(val);
        if (I) {
            I->eraseFromParent();
        } else {
            GlobalVariable *GV = dyn_cast<GlobalVariable>(val);
            if (GV)
                GV->eraseFromParent();
        }
    }

    void removeBlock(llvm::BasicBlock *blk) {
        for (auto *succ : successors(blk)) {
            // don't adjust phi nodes in this block if this is a self-loop,
            // we're gonna remove the block anyway
            if (succ == blk)
                continue;

            adjustPhiNodes(succ, blk);
        }

        // We need to drop the reference to this block in all
        // braching instructions that jump to this block.
        // See #99
        dropAllUses(blk);

        // we also must drop refrences to instructions that are in
        // this block (or we would need to delete the blocks in
        // post-dominator order), see #101
        for (auto &I : *blk)
            dropAllUses(&I);

        // finally, erase the block per se
        blk->eraseFromParent();
    }

    void slice(const std::vector<const llvm::Value *> &criteria) {
        auto slice = computeSlice(criteria);
        sliceModule(slice);
    }

  private:
    std::set<llvm::Value *>
    computeSlice(const std::vector<const llvm::Value *> &criteria);

    void sliceModule(const std::set<llvm::Value *> &slice);

    static void adjustPhiNodes(llvm::BasicBlock *pred, llvm::BasicBlock *blk) {
        using namespace llvm;

        for (Instruction &I : *pred) {
            PHINode *phi = dyn_cast<PHINode>(&I);
            if (phi) {
                // don't try remove block that we already removed
                int idx = phi->getBasicBlockIndex(blk);
                if (idx < 0)
                    continue;

                // the second argument is DeletePHIIFEmpty.
                // We don't want that, since that would make
                // dependence graph inconsistent. We'll
                // slice it away later, if it's empty
                phi->removeIncomingValue(idx, false);
            } else {
                // phi nodes are always at the beginning of the block
                // so if this is the first value that is not PHI,
                // there won't be any other and we can bail out
                break;
            }
        }
    }

    static inline bool shouldSliceInst(const llvm::Value *val) {
        using namespace llvm;
        const Instruction *Inst = dyn_cast<Instruction>(val);
        if (!Inst)
            return true;

        switch (Inst->getOpcode()) {
        case Instruction::Unreachable:
#if 0
            case Instruction::Br:
            case Instruction::Switch:
            case Instruction::Ret:
#endif
            return false;
        default:
            return true;
        }
    }

    /*
    static LLVMBBlock *createNewExitBB(LLVMDependenceGraph *graph) {
        using namespace llvm;

        LLVMBBlock *exitBB = new LLVMBBlock();

        Module *M = graph->getModule();
        LLVMContext &Ctx = M->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "safe_return");

        Value *fval = graph->getEntry()->getKey();
        Function *F = cast<Function>(fval);
        F->getBasicBlockList().push_back(block);

        // fill in basic block just with return value
        ReturnInst *RI;
        if (F->getReturnType()->isVoidTy())
            RI = ReturnInst::Create(Ctx, block);
        else if (F->getName().equals("main"))
            // if this is main, than the safe exit equals to returning 0
            // (it is just for convenience, we wouldn't need to do this)
            RI = ReturnInst::Create(
                    Ctx, ConstantInt::get(Type::getInt32Ty(Ctx), 0), block);
        else
            RI = ReturnInst::Create(Ctx, UndefValue::get(F->getReturnType()),
                                    block);

        LLVMNode *newRet = new LLVMNode(RI);
        graph->addNode(newRet);

        exitBB->append(newRet);
        exitBB->setKey(block);
        exitBB->setDG(graph);

        return exitBB;
    }

    static LLVMBBlock *addNewExitBB(LLVMDependenceGraph *graph) {
        // FIXME: don't create new one, create it
        // when creating graph and just use that one
        LLVMBBlock *newExitBB = createNewExitBB(graph);
        graph->setExitBB(newExitBB);
        graph->setExit(newExitBB->getLastNode());
        // do not add the block to the graph,
        // we'll do it at the end of adjustBBlocksSucessors,
        // because this function is called while iterating
        // over blocks, so that we won't corrupt the iterator

        return newExitBB;
    }

    bool dontTouch(const llvm::StringRef &r) {
        for (const char *n : dont_touch)
            if (r.equals(n))
                return true;

        return false;
    }
    static void ensureEntryBlock(LLVMDependenceGraph *graph) {
        using namespace llvm;

        Value *val = graph->getEntry()->getKey();
        Function *F = cast<Function>(val);

        // Function is empty, just bail out
        if (F->begin() == F->end())
            return;

        BasicBlock *entryBlock = &F->getEntryBlock();

        if (pred_begin(entryBlock) == pred_end(entryBlock)) {
            // entry block has no predecessor, we're ok
            return;
        }

        // it has some predecessor, create new one, that will just
        // jump on it
        LLVMContext &Ctx = graph->getModule()->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "single_entry");

        // jump to the old entry block
        BranchInst::Create(entryBlock, block);

        // set it as a new entry by pusing the block to the front
        // of the list
        F->getBasicBlockList().push_front(block);

        // FIXME: propagate this change to dependence graph
    }
    */

    // do not slice these functions at all
    std::set<const char *> dont_touch;
};

} // namespace llvmdg
} // namespace dg

#endif
