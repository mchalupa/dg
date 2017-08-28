#ifndef _DG_DEFUSE_H_
#define _DG_DEFUSE_H_

#include <unordered_map>
#include <vector>

#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/SubgraphNode.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {

using DefUseGraph = std::unordered_map<dg::analysis::rd::RDNode *, std::vector<dg::analysis::rd::RDNode *>>;

/**
 * Constructs Def->Use graph from RDNode-s
 */
class DugBuilder
{
private:
    static constexpr unsigned DFS = 10;
    using NodeT = dg::analysis::rd::RDNode;

    /**
     * Returns all nodes from graph with given root in BFS order
     */
    std::vector<NodeT *> bfs(NodeT *root)
    {
        assert(root && "need root");
        std::vector<NodeT *> result;

        unsigned dfsnum = DFS;

        ADT::QueueFIFO<NodeT *> fifo;
        fifo.push(root);
        root->dfsid = dfsnum;

        while (!fifo.empty()) {
            NodeT *cur = fifo.pop();
            result.push_back(cur);

            for (NodeT *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }

        return result;
    }

public:

    /**
     * Constructs def->use graph from given cfg.
     */
    DefUseGraph build(NodeT *root)
    {
        assert(root && "root may not be null");
        DefUseGraph result;
        std::vector<NodeT *> cfg = bfs(root);
        for (auto& def : cfg) {
            if (def->getType() != ALLOC)
                continue;

            std::vector<NodeT *> uses;
            for(auto& use : cfg) {
                if (use->getType() != STORE)
                    continue;

                if (use->defines(def)) {
                    uses.push_back(use);
                }
            }
            result[def] = std::move(uses);
        }
        return result;
    }
};

}
}
}
}

#endif // _DG_DEFUSE_H_
