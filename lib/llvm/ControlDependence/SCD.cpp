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

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
#include "llvm/Analysis/DominanceFrontier.h"
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif


#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
#include "dg/BFS.h"
#endif

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

using namespace std;

namespace dg {
namespace llvmdg {

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))

/// LLVM < 3.9 does not have post-dominance frontiers computation...
class PostDominanceFrontiers {
    using BBlockT = llvm::BasicBlock;
    using DomSetMapType = typename llvm::DominanceFrontierBase<llvm::BasicBlock>::DomSetMapType;
    using DomSetType = typename llvm::DominanceFrontierBase<llvm::BasicBlock>::DomSetType;

    DomSetMapType frontiers;

public:
    // based on the code from giri project by @liuml07,
    // https://github.com/liuml07/giri
    DomSetType& calculate(const llvm::PostDominatorTree &DT,
                          const llvm::DomTreeNode *Node) {

        llvm::BasicBlock *BB = Node->getBlock();
        DomSetType &S = frontiers[BB];
        if (DT.getRoots().empty())
            return S;

        // calculate DFlocal[Node]
        if (BB) {
          for (auto *P : predecessors(BB)) {
            // Does Node immediately dominate this predecessor?
            auto *SINode = DT[P];
            if (SINode && SINode->getIDom() != Node)
              S.insert(P);
          }
        }

        // At this point, S is DFlocal.  Now we union in DFup's of our children.
        // Loop through and visit the nodes that Node immediately dominates (Node's
        // children in the IDomTree)
        for (auto IDominee : *Node) {
          const auto &ChildDF = calculate(DT, IDominee);

          for (auto *cdfi : ChildDF) {
            if (!DT.properlyDominates(Node, DT[cdfi]))
              S.insert(cdfi);
          }
        }

        return S;
    }
};

#endif // LLVM < 3.9

void SCD::computePostDominators(llvm::Function& F) {
    DBG_SECTION_BEGIN(cda, "Computing post dominators for function "
                           << F.getName().str());
    using namespace llvm;

    PostDominatorTree *pdtree = nullptr;

    DBG(cda, "Computing post dominator tree");
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
    pdtree = new PostDominatorTree();
    // compute post-dominator tree for this function
    pdtree->runOnFunction(F);

    DBG(cda, "Computing post dominator frontiers and adding CD");

    PostDominanceFrontiers PDF;

    for (auto& B : F) {
        auto *pdtreenode = pdtree->getNode(&B);
        assert(pdtreenode && "Do not have a node in post-dom tree");
        auto& pdfrontiers = PDF.calculate(*pdtree, pdtreenode);
        for (auto *pdf : pdfrontiers) {
            dependencies[&B].insert(pdf);
            dependentBlocks[pdf].insert(&B);
        }
    }

    delete pdtree;
#else // LLVM >= 3.9

    PostDominatorTreeWrapperPass wrapper;
    wrapper.runOnFunction(F);
    pdtree = &wrapper.getPostDomTree();

#ifndef NDEBUG
    wrapper.verifyAnalysis();
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
#endif // LLVM < 3.9

    DBG_SECTION_END(cda, "Done computing post dominators for function " << F.getName().str());
}


} // namespace llvmdg
} // namespace dg
