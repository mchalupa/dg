#ifndef DG_DEPENDENCE_GRAPH_H_
#define DG_DEPENDENCE_GRAPH_H_

#include <assert.h>

namespace dg {
namespace sdg {

class DGNode;
class SystemDependenceGraph;

class DependenceGraph {
    friend class SystemDependenceGraph;

    unsigned _id{0};
    SystemDependenceGraph *_sdg{nullptr};

    std::set<DGNode *> _nodes;
    DependenceGraph(unsigned id, SystemDependenceGraph *g)
    : _id(id), _sdg(g) { assert(id > 0); }

public:
    unsigned getID() const { return _id; }
    SystemDependenceGraph *getSDG() { return _sdg; }
    const SystemDependenceGraph *getSDG() const { return _sdg; }
};

} // namespace sdg
} // namespace dg

#endif // DG_DEPENDENCE_GRAPH_H_
