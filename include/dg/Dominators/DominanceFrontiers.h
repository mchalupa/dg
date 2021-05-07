#ifndef DG_DOMINANCE_FRONTIERS_H_
#define DG_DOMINANCE_FRONTIERS_H_

#include <vector>

#include "BBlock.h"
#include "BFS.h"

namespace dg {

///
// Compute dominance frontiers
//
// \param root   root of dominators tree
//
// This algorithm takes dominator tree
// (edges of the tree are in BBlocks) and computes
// dominance frontiers for every node
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
template <typename NodeT>
class DominanceFrontiers {
    static void queueDomBBs(BBlock<NodeT> *BB,
                            std::vector<BBlock<NodeT> *> *blocks) {
        blocks->push_back(BB);
    }

    void computeDFrontiers(BBlock<NodeT> *X) {
        // DF_local
        for (const auto &edge : X->successors()) {
            BBlock<NodeT> *Y = edge.target;
            if (Y->getIDom() != X) {
                X->addDomFrontier(Y);
            }
        }

        // DF_up
        for (BBlock<NodeT> *Z : X->getDominators()) {
            for (BBlock<NodeT> *Y : Z->getDomFrontiers()) {
                if (Y->getIDom() != X) {
                    X->addDomFrontier(Y);
                }
            }
        }
    }

  public:
    void compute(BBlock<NodeT> *root) {
        std::vector<BBlock<NodeT> *> blocks;
        BBlockBFS<NodeT> bfs(BFS_BB_DOM);

        // get BBs in the order of dom tree edges (BFS),
        // so that we process it bottom-up
        bfs.run(root, queueDomBBs, &blocks);

        // go bottom-up the dom tree and compute domninance frontiers
        for (int i = blocks.size() - 1; i >= 0; --i)
            computeDFrontiers(blocks[i]);
    }
};

} // namespace dg

#endif
