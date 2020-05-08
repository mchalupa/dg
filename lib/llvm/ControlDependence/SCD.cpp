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

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/Dominators/PostDominanceFrontiers.h"
#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

using namespace std;

namespace dg {
namespace llvmdg {

///
// Compute post-dominance frontiers
//
// This algorithm takes post-dominator tree
// and computes post-dominator frontiers for every node
//
// The algorithm is due:
//
// R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, and F. K. Zadeck. 1989.
// An efficient method of computing static single assignment form.
// In Proceedings of the 16th ACM SIGPLAN-SIGACT symposium on Principles of programming languages (POPL '89),
// CORPORATE New York, NY Association for Computing Machinery (Ed.). ACM, New York, NY, USA, 25-35.
// DOI=http://dx.doi.org/10.1145/75277.75280
//
class PostDominanceFrontiers {
public:
    using FrontiersMap
        = std::unordered_map<llvm::BasicBlock *, std::set<llvm::BasicBlock *>>;
private:

    FrontiersMap frontiers;

    void computePDFrontiers(llvm::PostDominatorTree *tree,
                            const llvm::DomTreeNode *node) {
        auto *block = node->getBlock();

        llvm::SmallVector<llvm::BasicBlock *, 4> desc;
        tree->getDescendants(block, desc);

        // (1) process children
        // its a tree, we do not need to track the visited nodes :)
        for (auto *d : desc) {
            auto *dnode = tree->getNode(d);
            if (dnode == node) {
                continue;
            }
            computePDFrontiers(tree, tree->getNode(d));
        }

        if (block == nullptr) {
            return; // the virtual root
        }

        // (2) process this node
        // compute DFlocal
        for (auto *pred : predecessors(block)) {
            auto *prednode = tree->getNode(pred);
            auto *ipdom = prednode->getIDom();
            if (ipdom && ipdom != node) {
                frontiers[block].insert(pred);
            }
        }

        // iterate over immediate postdominators of 'node'
        desc.clear();
        tree->getDescendants(block, desc);
        for (auto *pdom : desc) {
            for (auto *df : frontiers[pdom]) {
                auto *dfnode = tree->getNode(df);
                auto *ipdom = dfnode->getIDom();
                if (ipdom && ipdom != node && dfnode != node) {
                    frontiers[block].insert(df);
                }
            }
        }
    }

public:
    const FrontiersMap& getFrontiners() const { return frontiers; }

    void compute(llvm::PostDominatorTree *tree) {
        assert(getFrontiners().empty()
               && "Already computed PD frontiers");

        // FIXME: reserve the memory for frontiers
        auto *root = tree->getRootNode();
        computePDFrontiers(tree, root);
    }
};

void SCD::computePostDominators(llvm::Function& F) {
    DBG_SECTION_BEGIN(cda, "Computing post dominators for function " << F.getName().str());
    using namespace llvm;

    PostDominatorTree *pdtree = nullptr;

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

    PostDominanceFrontiers pdfrontiers;
    pdfrontiers.compute(pdtree);
    const auto& frontiers = pdfrontiers.getFrontiners();

    // FIXME: reserve the memory
    for (auto& it : frontiers) {
        for (auto *dep : it.second) {
            dependentBlocks[dep].insert(it.first);
        }
        dependencies[it.first] = std::move(it.second);
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
     delete pdtree;
#endif
    DBG_SECTION_END(cda, "Done computing post dominators for function " << F.getName().str());
}


} // namespace llvmdg
} // namespace dg
