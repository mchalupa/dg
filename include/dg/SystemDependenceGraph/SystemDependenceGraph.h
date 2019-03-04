#ifndef DG_SYSTEM_DEPENDENCE_GRAPH_H_
#define DG_SYSTEM_DEPENDENCE_GRAPH_H_

#include <memory>
#include <vector>
#include <set>

#include "dg/SystemDependenceGraph/DependenceGraph.h"

namespace dg {

class DGNode;

class SystemDependenceGraph {
    std::set<DGNode *> _globals;
    std::vector<std::unique_ptr<DependenceGraph>> _graphs;
    DependenceGraph* _entry{nullptr};

public:
    DependenceGraph *getEntry() { return _entry; }
    const DependenceGraph *getEntry() const { return _entry; }
    void setEntry(DependenceGraph *g) { _entry = g; }

    DependenceGraph *createGraph() {
        _graphs.emplace_back(new DependenceGraph(_graphs.size(), this));
        return _graphs.back().get();
    }
};

} // namespace dg

#endif // _DG_DEPENDENCE_GRAPH_H_
