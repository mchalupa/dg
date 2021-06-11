#ifndef DG_DGBBLOCK_H_
#define DG_DGBBLOCK_H_

#include <cassert>
#include <vector>

#include "DGNode.h"
#include "DepDGElement.h"

namespace dg {
namespace sdg {

class DependenceGraph;

///
// A basic block of a dependence graph.
// Basic blocks are useful even in dependence graph in order
// to cluster nodes with the same control dependence.
class DGBBlock : public DepDGElement {
    friend class DependenceGraph;
    using NodesTy = std::vector<DGNode *>;

    NodesTy _nodes;
    DGBBlock(DependenceGraph &g) : DepDGElement(g, DGElementType::BBLOCK) {}

  public:
    static DGBBlock *get(DGElement *elem) {
        return elem->getType() == DGElementType::BBLOCK
                       ? static_cast<DGBBlock *>(elem)
                       : nullptr;
    }

    NodesTy &getNodes() { return _nodes; }
    const NodesTy &getNodes() const { return _nodes; }

    void append(DGNode *n) {
        assert(n && "nullptr passed as node");
        assert(n->getBBlock() == nullptr && "BBlock already set");
        _nodes.push_back(n);
        n->setBBlock(this);
    }

    DGNode *front() { return _nodes.front(); }
    DGNode *back() { return _nodes.back(); }
    const DGNode *front() const { return _nodes.front(); }
    const DGNode *back() const { return _nodes.back(); }
};

} // namespace sdg
} // namespace dg

#endif // DG_DGBBLOCK_H_
