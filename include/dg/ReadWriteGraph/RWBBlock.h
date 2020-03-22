#ifndef DG_RWBBLOCK_H_
#define DG_RWBBLOCK_H_

#include <list>
#include <memory>

#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

template <typename BBlockT>
class BBlockBase {
    using EdgesT = std::vector<BBlockT *>;

protected:
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

    bool hasSuccessors() const { return !_successors.empty(); }
    bool hasPredecessors() const { return !_predecessors.empty(); }

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
    RWSubgraph *subgraph{nullptr};

public:

    using NodeT = RWNode;
    using NodesT = std::list<NodeT *>;

    RWBBlock() = default;
    RWBBlock(RWSubgraph *s) : subgraph(s) {}

    RWSubgraph *getSubgraph() { return subgraph; }
    const RWSubgraph *getSubgraph() const { return subgraph; }

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

    /*
    // split the block before the given node and return the later
    // part (the one including n). If 'node' is the first node
    // of this block, return nullptr;
    std::unique_ptr<RWBBlock> splitBefore(NodeT *node) {
        assert(node->getBBlock() == this
               && "Spliting a block on invalid node");

#ifndef NDEBUG
        auto old_size = _nodes.size();
#endif
        unsigned num = 0;
        auto it = _nodes.begin(), et = _nodes.end();
        for (; it != et; ++it) {
            if (*it == node) {
                break;
            }
            ++num;
        }

        assert(*it == node);

        if (num == 0) {
            return nullptr;
        }

        auto newblock = std::unique_ptr<RWBBlock>(new RWBBlock(subgraph));
        for (; it != et; ++it) {
            newblock->_nodes.push_back(*it);
        }

        assert(newblock->size() >= 1 && "New block must contain at least a node");

        // truncate nodes in this block
        _nodes.resize(num);

        assert(_nodes.size() + newblock->size() == old_size
               && "Bug in splitting nodes");

        return newblock;
    }
    */

    // Split the block before and after the given node.
    // Return newly created basic blocks (there are at most two of them).
    std::pair<std::unique_ptr<RWBBlock>, std::unique_ptr<RWBBlock>>
    splitAround(NodeT *node) {
        assert(node->getBBlock() == this
               && "Spliting a block on invalid node");

        RWBBlock *withnode = nullptr;
        RWBBlock *after = nullptr;

        if (_nodes.size() == 1) {
            assert(*_nodes.begin() == node);
            return {nullptr, nullptr};
        }

#ifndef NDEBUG
        auto old_size = _nodes.size();
        assert(old_size > 1);
#endif
        unsigned num = 0;
        auto it = _nodes.begin(), et = _nodes.end();
        for (; it != et; ++it) {
            if (*it == node) {
                break;
            }
            ++num;
        }

        assert(it != et && "Did not find the node");
        assert(*it == node);

        ++it;
        if (it != et) {
            after = new RWBBlock(subgraph);
            for (; it != et; ++it) {
                after->append(*it);
            }
        }

        // truncate nodes in this block
        if (num > 0) {
            withnode = new RWBBlock(subgraph);
            withnode->append(node);

            _nodes.resize(num);
        } else {
            assert(*_nodes.begin() == node);
            assert(after && "Should have a suffix");
            _nodes.resize(1);
        }

        assert(!withnode || withnode->size() == 1);
        assert(((_nodes.size() +
                (withnode ? withnode->size() : 0) +
                (after ? after->size() : 0)) == old_size)
               && "Bug in splitting nodes");

        // reconnect edges
        RWBBlock *bbwithsuccessors = after;
        if (!bbwithsuccessors) // no suffix
            bbwithsuccessors = withnode;

        assert(bbwithsuccessors);
        for (auto *s : this->_successors) {
            for (auto& p : s->_predecessors) {
                if (p == this) {
                    p = bbwithsuccessors;
                }
            }
        }
        // swap this and after successors
        bbwithsuccessors->_successors.swap(this->_successors);

        if (withnode) {
            this->addSuccessor(withnode);
            if (after) {
                withnode->addSuccessor(after);
            }
        } else {
            assert(after && "Should have a suffix");
            this->addSuccessor(after);
        }

        return {std::unique_ptr<RWBBlock>(withnode),
                std::unique_ptr<RWBBlock>(after)};
    }


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
