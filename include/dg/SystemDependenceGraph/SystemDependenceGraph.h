#ifndef DG_SYSTEM_DEPENDENCE_GRAPH_H_
#define DG_SYSTEM_DEPENDENCE_GRAPH_H_

#include <memory>
#include <set>
#include <vector>

#include "dg/SystemDependenceGraph/DependenceGraph.h"

namespace dg {
// Use the namespace sdg for now to avoid name collisions.
// When this class is finished and working, we'll remove
// the old dependence graph class and this namespace.
namespace sdg {

class DGNode;

class SystemDependenceGraph {
    std::set<DGNode *> _globals;
    std::vector<std::unique_ptr<DependenceGraph>> _graphs;
    DependenceGraph *_entry{nullptr};

    // wrapper around graphs iterator that unwraps the unique_ptr
    struct graphs_iterator : public decltype(_graphs.begin()) {
        using OrigItType = decltype(_graphs.begin());

        graphs_iterator() = default;
        graphs_iterator(const graphs_iterator &I) = default;
        graphs_iterator(const OrigItType &I) : OrigItType(I) {}

        DependenceGraph *operator*() { return OrigItType::operator*().get(); }
        // DependenceGraph* operator->() { return OrigItType::operator*().get();
        // }
    };

  public:
    DependenceGraph *getEntry() { return _entry; }
    const DependenceGraph *getEntry() const { return _entry; }
    void setEntry(DependenceGraph *g) { _entry = g; }

    DependenceGraph &createGraph() {
        _graphs.emplace_back(new DependenceGraph(_graphs.size() + 1, *this));
        return *_graphs.back().get();
    }

    DependenceGraph &createGraph(const std::string &name) {
        auto &g = createGraph();
        g.setName(name);
        return g;
    }

    size_t size() const { return _graphs.size(); }

    graphs_iterator begin() { return graphs_iterator(_graphs.begin()); }
    graphs_iterator end() { return graphs_iterator(_graphs.end()); }
};

} // namespace sdg
} // namespace dg

#endif // _DG_DEPENDENCE_GRAPH_H_
