#include "SCD.h"

#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/PostDominators.h>
#include "llvm/Analysis/IteratedDominanceFrontier.h"

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

using namespace std;

namespace dg {
namespace llvmdg {

void SCD::computePostDominators(llvm::Function& F) {
    DBG_SECTION_BEGIN(cda, "Computing post dominators for function "
                           << F.getName().str());
    using namespace llvm;

    PostDominatorTree *pdtree = nullptr;

    DBG(cda, "Computing post dominator tree");
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
    pdtree = new PostDominatorTree();
    // compute post-dominator tree for this function
    pdtree->runOnFunction(f);
#else
    PostDominatorTreeWrapperPass wrapper;
    wrapper.runOnFunction(F);
    pdtree = &wrapper.getPostDomTree();
#ifndef NDEBUG
    wrapper.verifyAnalysis();
#endif
#endif

    DBG(cda, "Computing post dominator frontiers and adding CD");
    llvm::ReverseIDFCalculator PDF(*pdtree);
    for (auto& B : F) {
        llvm::SmallPtrSet<llvm::BasicBlock *, 1> blocks;
        blocks.insert(&B);
        PDF.setDefiningBlocks(blocks);

        SmallVector<BasicBlock *, 8> pdfrontiers;
        PDF.calculate(pdfrontiers);

        // FIXME: reserve the memory
        for (auto *pdf : pdfrontiers) {
            dependencies[&B].insert(pdf);
            dependentBlocks[pdf].insert(&B);
        }
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
     delete pdtree;
#endif
    DBG_SECTION_END(cda, "Done computing post dominators for function " << F.getName().str());
}


} // namespace llvmdg
} // namespace dg
