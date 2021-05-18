#ifndef DG_SCC_H_
#define DG_SCC_H_

#include <set>
#include <vector>

#include "dg/ADT/Queue.h"
#include "dg/ADT/STLHashMap.h"

namespace dg {

// implementation of tarjan's algorithm for
// computing strongly connected components
// for a directed graph that has a starting vertex
// from which are all other vertices reachable
template <typename NodeT>
class SCC {
  public:
    using SCC_component_t = std::vector<NodeT *>;
    using SCC_t = std::vector<SCC_component_t>;

    SCC<NodeT>() = default;

    // returns a vector of vectors - every inner vector
    // contains the nodes contained in one SCC
    SCC_t &compute(NodeT *start) {
        _compute(start);
        assert(stack.empty());

        return scc;
    }

    const SCC_t &getSCC() const { return scc; }

    SCC_component_t &operator[](unsigned idx) {
        assert(idx < scc.size());
        return scc[idx];
    }

  private:
    struct NodeInfo {
        unsigned dfs_id{0};
        unsigned lowpt{0};
        bool on_stack{false};
    };

    ADT::QueueLIFO<NodeT *> stack;
    CachingHashMap<NodeT *, NodeInfo> _info;
    unsigned index{0};

    // container for the strongly connected components.
    SCC_t scc;

    void _compute(NodeT *n) {
        auto &info = _info[n];
        // here we using the fact that we are a friend class
        // of SubgraphNode. If we would need to make this
        // algorithm more generinc, we add setters/getters.
        info.dfs_id = info.lowpt = ++index;
        info.on_stack = true;
        stack.push(n);

        for (auto *succ : n->successors()) {
            auto &succ_info = _info[succ];
            if (succ_info.dfs_id == 0) {
                assert(!succ_info.on_stack);
                _compute(succ);
                info.lowpt = std::min(info.lowpt, succ_info.lowpt);
            } else if (succ_info.on_stack) {
                info.lowpt = std::min(info.lowpt, succ_info.dfs_id);
            }
        }

        if (info.lowpt == info.dfs_id) {
            SCC_component_t component;
            size_t component_num = scc.size();

            NodeT *w;
            while (_info[stack.top()].dfs_id >= info.dfs_id) {
                w = stack.pop();
                auto &winfo = _info[w];
                assert(winfo.on_stack == true);
                winfo.on_stack = false;
                component.push_back(w);
                // the numbers scc_id give
                // a reverse topological order
                w->setSCCId(component_num);

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
        const SCC_component_t &component;
        std::set<unsigned> _successors;

        Node(SCC_component_t &comp) : component(comp) {}

        void addSuccessor(unsigned idx) { _successors.insert(idx); }
        const SCC_component_t &operator*() const { return component; }
        // XXX: create iterators instead
        const std::set<unsigned> &successors() const { return _successors; }
    };

    std::vector<Node> nodes;

  public:
    Node &operator[](unsigned idx) {
        assert(idx < nodes.size());
        return nodes[idx];
    }

    void compute(SCC_t &scc) {
        // we know the size before-hand
        nodes.reserve(scc.size());

        // create the nodes in our condensation graph
        for (auto &comp : scc)
            nodes.push_back(Node(comp));

        assert(nodes.size() == scc.size());

        int idx = 0;
        for (auto &comp : scc) {
            for (NodeT *node : comp) {
                // we can get from this component
                // to the component of succ
                for (NodeT *succ : node->successors()) {
                    unsigned succ_idx = succ->getSCCId();
                    if (static_cast<int>(succ_idx) != idx)
                        nodes[idx].addSuccessor(succ_idx);
                }
            }

            ++idx;
        }
    }

    SCCCondensation<NodeT>() = default;
    SCCCondensation<NodeT>(SCC<NodeT> &S) { compute(S.getSCC()); }

    SCCCondensation<NodeT>(SCC_t &s) { compute(s); }
};

} // namespace dg

#endif
