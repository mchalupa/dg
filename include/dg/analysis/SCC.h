#ifndef _DG_SCC_H_
#define  _DG_SCC_H_

#include <vector>
#include <set>

#include "dg/ADT/Queue.h"

namespace dg {
namespace analysis {

// implementation of tarjan's algorithm for
// computing strongly connected components
// for a directed graph that has a starting vertex
// from which are all other vertices reachable
template <typename NodeT>
class SCC {
public:
    using SCC_component_t = std::vector<NodeT *>;
    using SCC_t = std::vector<SCC_component_t>;

    SCC<NodeT>(unsigned not_visit = 0)
    : index(not_visit), NOT_VISITED(not_visit) {}

    // returns a vector of vectors - every inner vector
    // contains the nodes that for a SCC
    SCC_t& compute(NodeT *start)
    {
        _compute(start);
        assert(stack.empty());

        return scc;
    }

    const SCC_t& getSCC() const
    {
        return scc;
    }

    SCC_component_t& operator[](unsigned idx)
    {
        assert(idx < scc.size());
        return scc[idx];
    }

    unsigned getIndex() const { return index; }

private:
    ADT::QueueLIFO<NodeT *> stack;
    unsigned index;
    // it the dfsid is less or equal to this value,
    // then it is considered not to be visited.
    // it is due to the repeated execution of this algorithm
    // on the same nodes
    const unsigned NOT_VISITED;

    // container for the strongly connected components.
    SCC_t scc;

    bool not_visited(NodeT *n) { return n->dfs_id <= NOT_VISITED; }

    void _compute(NodeT *n)
    {
        // here we using the fact that we are a friend class
        // of SubgraphNode. If we would need to make this
        // algorithm more generinc, we add setters/getters.
        n->dfs_id = n->lowpt = ++index;
        stack.push(n);
        n->on_stack = true;

        for (NodeT *succ : n->getSuccessors()) {
            if (not_visited(succ)) {
                assert(!succ->on_stack);
                _compute(succ);
                n->lowpt = std::min(n->lowpt, succ->lowpt);
            } else if (succ->on_stack) {
                n->lowpt = std::min(n->lowpt, succ->dfs_id);
            }
        }

        if (n->lowpt == n->dfs_id) {
            SCC_component_t component;
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
};

template <typename NodeT>
class SCCCondensation {
    using SCC_t = typename SCC<NodeT>::SCC_t;
    using SCC_component_t = typename SCC<NodeT>::SCC_component_t;

    struct Node {
        const SCC_component_t& component;
        std::set<unsigned> successors;

        Node(SCC_component_t& comp) : component(comp) {}

        void addSuccessor(unsigned idx)
        {
            successors.insert(idx);
        }

        const SCC_component_t& operator*() const
        {
            return component;
        }

        // XXX: create iterators instead
        const std::set<unsigned>& getSuccessors() const
        {
            return successors;
        }
    };

    std::vector<Node> nodes;

public:
    Node& operator[](unsigned idx)
    {
        assert(idx < nodes.size());
        return nodes[idx];
    }

    void compute(SCC_t& scc)
    {
        // we know the size before-hand
        nodes.reserve(scc.size());

        // create the nodes in our condensation graph
        for (auto& comp : scc)
            nodes.push_back(Node(comp));

        assert(nodes.size() == scc.size());

        int idx = 0;
        for (auto& comp : scc) {
            for (NodeT *node : comp) {
                // we can get from this component
                // to the component of succ
                for (NodeT *succ : node->getSuccessors()) {
                    unsigned succ_idx = succ->getSCCId();
                    if (static_cast<int>(succ_idx) != idx)
                        nodes[idx].addSuccessor(succ_idx);
                }
            }

            ++idx;
        }
    }

    SCCCondensation<NodeT>() = default;
    SCCCondensation<NodeT>(SCC<NodeT>& S)
    {
        compute(S.getSCC());
    }

    SCCCondensation<NodeT>(SCC_t& s)
    {
        compute(s);
    }
};

} // analysis
} // dg
#endif //  _DG_SCC_H_
