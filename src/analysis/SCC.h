#ifndef _DG_SCC_H_
#define  _DG_SCC_H_

#include <vector>

#include "ADT/Queue.h"

namespace dg {
namespace analysis {

// implementation of tarjan's algorithm for
// computing strongly connected components
// for a directed graph that has a starting vertex
// from which are all other vertices reachable
template <typename NodeT>
class SCC {
    ADT::QueueLIFO<NodeT *> stack;
    unsigned index;

    std::vector<std::vector<NodeT *>> scc;

    void _compute(NodeT *n)
    {
        // here we using the fact that we are a friend class
        // of SubgraphNode. If we would need to make this
        // algorithm more generinc, we add setters/getters.
        n->dfs_id = n->lowpt = ++index;
        stack.push(n);
        n->on_stack = true;

        for (NodeT *succ : n->getSuccessors()) {
            if (succ->dfs_id == 0) {
                assert(!succ->on_stack);
                _compute(succ);
                n->lowpt = std::min(n->lowpt, succ->lowpt);
            } else if (succ->on_stack) {
                n->lowpt = std::min(n->lowpt, succ->dfs_id);
            }
        }

        if (n->lowpt == n->dfs_id) {
            std::vector<NodeT *> component;
            size_t component_num = scc.size();

            NodeT *w;
            while (stack.top()->dfs_id >= n->dfs_id) {
                w = stack.pop();
                w->on_stack = false;
                component.push_back(w);
                // the numbers scc_id give
                // a reverse topological order
                w->scc_id = component_num;

                if (stack.empty())
                    break;
            }

            scc.push_back(std::move(component));
        }
    }

public:
    SCC<NodeT>() : index(0) {}

    // returns a vector of vectors - every inner vector
    // contains the nodes that for a SCC
    std::vector<std::vector<NodeT *>>& compute(NodeT *start)
    {
        assert(start->dfs_id == 0);

        _compute(start);
        return scc;
    }

};

} // analysis
} // dg
#endif //  _DG_SCC_H_
