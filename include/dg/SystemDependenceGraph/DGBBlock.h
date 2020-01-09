#ifndef DG_DGBBLOCK_H_
#define DG_DGBBLOCK_H_

#include <assert.h>

#include "DGNode.h"

namespace dg {
namespace sdg {

class DependenceGraph;

///
// A basic block of a dependence graph.
// Basic blocks are useful even in dependence graph in order
// to cluster nodes with the same control dependence.
class DGBBlock {
    friend class DependenceGraph;
    using NodesTy = std::vector<DGNode *>;

    unsigned _id{0};
    // SDG to which this dependence graph belongs
    DependenceGraph *_dg{nullptr};

    NodesTy _nodes;
    DGBBlock(unsigned id, DependenceGraph *g)
    : _id(id), _dg(g) { assert(id > 0); }

public:
    unsigned getID() const { return _id; }

    DependenceGraph *getDG() { return _dg; }
    const DependenceGraph *getDG() const { return _dg; }

    NodesTy& getNodes() { return _nodes; }
    const NodesTy& getNodes() const { return _nodes; }

    void append(DGNode *n) {
        assert(n && "nullptr passed as node");
        assert(n->getBBlock() == nullptr && "BBlock already set");
        _nodes.push_back(n);
        n->setBBlock(this);
    }

};

} // namespace sdg
} // namespace dg

#endif // DG_DGBBLOCK_H_
