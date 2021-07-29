#include "SCD.h"

#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Function.h>
//#include "llvm/Analysis/IteratedDominanceFrontier.h"

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

using namespace std;

namespace dg {
namespace llvmdg {

class PostDominanceFrontiers {
    using BBlockT = llvm::BasicBlock;
#if (LLVM_VERSION_MAJOR < 5)
    using DomSetMapType = typename llvm::DominanceFrontierBase<
            llvm::BasicBlock>::DomSetMapType;
    using DomSetType =
            typename llvm::DominanceFrontierBase<llvm::BasicBlock>::DomSetType;
#else
    using DomSetMapType =
            typename llvm::DominanceFrontierBase<llvm::BasicBlock,
                                                 true>::DomSetMapType;
    using DomSetType = typename llvm::DominanceFrontierBase<llvm::BasicBlock,
                                                            true>::DomSetType;
#endif

    DomSetMapType frontiers;

  public:
    // based on the code from giri project by @liuml07,
    // https://github.com/liuml07/giri
    DomSetType &calculate(const llvm::PostDominatorTree &DT,
                          const llvm::DomTreeNode *Node) {
        llvm::BasicBlock *BB = Node->getBlock();
        auto &S = frontiers[BB];

#if LLVM_VERSION_MAJOR >= 11
        bool empty_roots = DT.root_size() == 0;
#else
        bool empty_roots = DT.getRoots().empty();
#endif
        if (empty_roots)
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
        // Loop through and visit the nodes that Node immediately dominates
        // (Node's children in the IDomTree)
        for (auto *IDominee : *Node) {
            const auto &ChildDF = calculate(DT, IDominee);

            for (auto *cdfi : ChildDF) {
                if (!DT.properlyDominates(Node, DT[cdfi]))
                    S.insert(cdfi);
            }
        }

        return S;
    }
};

void SCD::computePostDominators(llvm::Function &F) {
    DBG_SECTION_BEGIN(cda, "Computing post dominators for function "
                                   << F.getName().str());
    using namespace llvm;

    PostDominatorTree *pdtree = nullptr;

    DBG(cda, "Computing post dominator tree");
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))

    auto pdtreeptr =
            std::unique_ptr<PostDominatorTree>(new PostDominatorTree());
    pdtree = pdtreeptr.get();
    pdtree->runOnFunction(F); // compute post-dominator tree for this function

#else // LLVM >= 3.9

    PostDominatorTreeWrapperPass wrapper;
    wrapper.runOnFunction(F);
    pdtree = &wrapper.getPostDomTree();

#ifndef NDEBUG
    wrapper.verifyAnalysis();
#endif

    DBG(cda, "Computing post dominator frontiers and adding CD");

#if 0 // this does not work as expected
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
#endif

    PostDominanceFrontiers PDF;

    for (auto &B : F) {
        auto *pdtreenode = pdtree->getNode(&B);
        assert(pdtreenode && "Do not have a node in post-dom tree");
        auto &pdfrontiers = PDF.calculate(*pdtree, pdtreenode);
        for (auto *pdf : pdfrontiers) {
            dependencies[&B].insert(pdf);
            dependentBlocks[pdf].insert(&B);
        }
    }

#endif // LLVM < 3.9

    DBG_SECTION_END(cda, "Done computing post dominators for function "
                                 << F.getName().str());
}

} // namespace llvmdg
} // namespace dg
