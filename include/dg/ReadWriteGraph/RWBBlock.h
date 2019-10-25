#ifndef DG_RWBBLOCK_H_
#define DG_RWBBLOCK_H_

#include <list>

#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

class RWBBlock {
    void _check() {
#ifndef NDEBUG
        // first node can have several predecessors
        // and the last node can have several successors,
        // otherwise the structure must be a chain
        if (_nodes.size() <= 1)
            return;

        auto it = _nodes.begin();
        assert((*it)->getSuccessors().size() == 1);
        do {
            ++it;
            if (it == _nodes.end())
                return;
            assert((*it)->getPredecessors().size() == 1);
            assert((*it)->getSuccessors().size() == 1 || ++it == _nodes.end());
        } while (it != _nodes.end());
#endif // not NDEBUG
    }

public:
    using NodeT = RWNode;
    using NodeSuccIterator = decltype(NodeT().getSuccessors().begin());
    using NodesT = std::list<NodeT *>;

    void append(NodeT *n) { _nodes.push_back(n); n->setBBlock(this); _check(); }
    void prepend(NodeT *n) { _nodes.push_front(n); n->setBBlock(this); _check(); }
    // FIXME: get rid of this method in favor of either append/prepend
    // (so these method would update CFG edges) or keeping CFG
    // only in blocks
    void prependAndUpdateCFG(NodeT *n) {
        // precondition for this method,
        // we can fix it at some point
        assert(!_nodes.empty());

        assert(n->getSuccessors().empty());
        assert(n->getPredecessors().empty());

        // update CFG edges
        n->insertBefore(_nodes.front());

        prepend(n);
        assert(!n->getSuccessors().empty());
        assert(n->getBBlock() == this);
        assert(n->getSingleSuccessor()->getBBlock() == this);

        _check();
    }

    const NodesT& getNodes() const { return _nodes; }

    // override the operator* method in the successor/predecessor iterator of the node
    struct edge_iterator : public NodeSuccIterator {
        edge_iterator() = default;
        edge_iterator(const NodeSuccIterator& I) : NodeSuccIterator(I) {}

        RWBBlock *operator*() { return NodeSuccIterator::operator*()->getBBlock(); }
        RWBBlock *operator->() { return NodeSuccIterator::operator*()->getBBlock(); }
    };

    edge_iterator pred_begin() { return edge_iterator(_nodes.front()->getPredecessors().begin()); }
    edge_iterator pred_end() { return edge_iterator(_nodes.front()->getPredecessors().end()); }
    edge_iterator succ_begin() { return edge_iterator(_nodes.back()->getSuccessors().begin()); }
    edge_iterator succ_end() { return edge_iterator(_nodes.back()->getSuccessors().end()); }

    RWBBlock *getSinglePredecessor() {
        auto& preds = _nodes.front()->getPredecessors();
        return preds.size() == 1 ? (*preds.begin())->getBBlock() : nullptr;
    }

    RWBBlock *getSingleSuccessor() {
        auto& succs = _nodes.back()->getSuccessors();
        return succs.size() == 1 ? (*succs.begin())->getBBlock() : nullptr;
    }

    NodeT *getFirst() { return _nodes.empty() ? nullptr : _nodes.front(); }
    NodeT *getLast() { return _nodes.empty() ? nullptr : _nodes.back(); }

private:
    NodesT _nodes;
};

} // namespace dda
} // namespace dg

#endif //DG_RWBBLOCK_H_
