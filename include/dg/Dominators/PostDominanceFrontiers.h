#ifndef DG_POST_DOMINANCE_FRONTIERS_H_
#define DG_POST_DOMINANCE_FRONTIERS_H_

#include <vector>

#include "dg/BBlock.h"
#include "dg/BFS.h"

namespace dg {
namespace legacy {

///
// Compute post-dominance frontiers
//
// \param root   root of post-dominators tree
//
// This algorithm takes post-dominator tree
// (edges of the tree are in BBlocks) and computes
// post-dominator frontiers for every node
//
// The algorithm is due:
//
// R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, and F. K. Zadeck. 1989.
// An efficient method of computing static single assignment form.
// In Proceedings of the 16th ACM SIGPLAN-SIGACT symposium on Principles of
// programming languages (POPL '89), CORPORATE New York, NY Association for
// Computing Machinery (Ed.). ACM, New York, NY, USA, 25-35.
// DOI=http://dx.doi.org/10.1145/75277.75280
//
template <typename NodeT, typename BBlockT>
class PostDominanceFrontiers {
    void computePDFrontiers(BBlockT *BB, bool add_cd) {
        // compute DFlocal
        for (auto *pred : BB->predecessors()) {
            auto *ipdom = pred->getIPostDom();
            if (ipdom && ipdom != BB) {
                BB->addPostDomFrontier(pred);

                // pd-frontiers are the reverse control dependencies
                if (add_cd)
                    pred->addControlDependence(BB);
            }
        }

        for (auto *pdom : BB->getPostDominators()) {
            for (auto *df : pdom->getPostDomFrontiers()) {
                auto *ipdom = df->getIPostDom();
                if (ipdom && ipdom != BB && df != BB) {
                    BB->addPostDomFrontier(df);

                    if (add_cd)
                        df->addControlDependence(BB);
                }
            }
        }
    }

  public:
    void compute(BBlockT *root, bool add_cd = false) {
        std::vector<BBlockT *> blocks;

        struct EdgeChooser {
            class range {
                BBlockT *_blk;

              public:
                range(BBlockT *blk) : _blk(blk) {}

                auto begin() -> decltype(_blk->getPostDominators().begin()) {
                    return _blk->getPostDominators().begin();
                }

                auto end() -> decltype(_blk->getPostDominators().end()) {
                    return _blk->getPostDominators().end();
                }
            };

            range operator()(BBlockT *b) const { return range(b); }
        };

        EdgeChooser chooser;
        BFS<BBlockT, SetVisitTracker<BBlockT>, EdgeChooser> bfs(chooser);

        // get BBs in the order of post-dom tree edges (BFS),
        // so that we process it bottom-up
        bfs.run(root, [&blocks](BBlockT *b) { blocks.push_back(b); });

        // go bottom-up the post-dom tree and compute post-domninance frontiers
        for (int i = blocks.size() - 1; i >= 0; --i)
            computePDFrontiers(blocks[i], add_cd);
    }
};

} // namespace legacy
} // namespace dg

#endif
