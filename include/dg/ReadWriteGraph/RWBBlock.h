#ifndef DG_RWBBLOCK_H_
#define DG_RWBBLOCK_H_

#include <list>

#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

template <typename BBlockT>
class BBlockBase {
    using EdgesT = std::vector<BBlockT *>;

    EdgesT _successors;
    EdgesT _predecessors;

public:
    auto succ_begin() -> decltype(_successors.begin()) { return _successors.begin(); }
    auto succ_end() -> decltype(_successors.begin())  { return _successors.end(); }
    auto pred_begin() -> decltype(_predecessors.begin()) { return _predecessors.begin(); }
    auto pred_end() -> decltype(_predecessors.begin()) { return _predecessors.end(); }
    auto succ_begin() const -> decltype(_successors.begin()) { return _successors.begin(); }
    auto succ_end() const -> decltype(_successors.begin())  { return _successors.end(); }
    auto pred_begin() const -> decltype(_predecessors.begin()) { return _predecessors.begin(); }
    auto pred_end() const -> decltype(_predecessors.begin()) { return _predecessors.end(); }

    const EdgesT getSuccessors() const { return _successors; }
    const EdgesT getPredecessors() const { return _predecessors; }

    void addSuccessor(BBlockT *s) {
        for (auto *succ : _successors) {
            if (succ == s)
                return;
        }

        _successors.push_back(s);

        for (auto *pred : s->_predecessors) {
            if (pred == this)
                return;
        }
        s->_predecessors.push_back(static_cast<BBlockT*>(this));
    }

    BBlockT *getSinglePredecessor() {
        return _predecessors.size() == 1 ? _predecessors.back() : nullptr;
    }

    BBlockT *getSingleSuccessor() {
        return _successors.size() == 1 ? _successors.back() : nullptr;
    }
};

class RWBBlock;

class RWBBlock : public BBlockBase<RWBBlock> {

public:
    using NodeT = RWNode;
    using NodesT = std::list<NodeT *>;

    // FIXME: move also this into BBlockBase
    void append(NodeT *n) { _nodes.push_back(n); n->setBBlock(this); }
    void prepend(NodeT *n) { _nodes.push_front(n); n->setBBlock(this); }

    void insertBefore(NodeT *n, NodeT *before) {
        assert(!_nodes.empty());

        auto it = _nodes.begin();
        while (it != _nodes.end()) {
            if (*it == before)
                break;
            ++it;
        }
        assert(it != _nodes.end() && "Did not find 'before' node");

        _nodes.insert(it, n);
        n->setBBlock(this);
    }

    // FIXME: get rid of this method in favor of either append/prepend
    // (so these method would update CFG edges) or keeping CFG
    // only in blocks
    void prependAndUpdateCFG(NodeT *n) {
        // precondition for this method,
        // we can fix it at some point
        assert(!_nodes.empty());

        // update CFG edges
        n->insertBefore(_nodes.front());

        prepend(n);
        assert(n->getBBlock() == this);
    }

    const NodesT& getNodes() const { return _nodes; }

    /*
    auto begin() -> decltype(_nodes.begin()) { return _nodes.begin(); }
    auto begin() const -> decltype(_nodes.begin()) { return _nodes.begin(); }
    auto end() -> decltype(_nodes.end()) { return _nodes.end(); }
    auto end() const -> decltype(_nodes.end()) { return _nodes.end(); }
    */

    // FIXME: rename to first/front(), last/back()
    NodeT *getFirst() { return _nodes.empty() ? nullptr : _nodes.front(); }
    NodeT *getLast() { return _nodes.empty() ? nullptr : _nodes.back(); }

    bool empty() const { return _nodes.empty(); }
    size_t size() const { return _nodes.size(); }

private:
    NodesT _nodes;
};

} // namespace dda
} // namespace dg

#endif //DG_RWBBLOCK_H_
