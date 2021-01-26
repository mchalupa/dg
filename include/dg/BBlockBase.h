#ifndef DG_BBLOCK_BASE_H_
#define DG_BBLOCK_BASE_H_

#include <cassert>
#include <vector>
#include <list>

namespace dg {

class ElemId {
    static unsigned idcnt;
    unsigned id;
public:
    ElemId() : id(++idcnt) {}
    unsigned getID() const { return id; }
};

template <typename ElemT>
class ElemWithEdges {
    using EdgesT = std::vector<ElemT *>;

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

    const EdgesT& successors() const { return _successors; }
    const EdgesT& predecessors() const { return _predecessors; }

    bool hasSuccessors() const { return !_successors.empty(); }
    bool hasPredecessors() const { return !_predecessors.empty(); }

    bool hasSuccessor(ElemT *s) const {
        for (const auto *succ : _successors) {
            if (succ == s)
                return true;
        }
        return false;
    }

    bool hasPredecessor(ElemT *s) const {
        for (auto *pred : _predecessors) {
            if (pred == s)
                return true;
        }
        return false;
    }

    void addSuccessor(ElemT *s) {
        if (hasSuccessor(s)) {
            assert(s->hasPredecessor(static_cast<ElemT*>(this)));
            return;
        }

        _successors.push_back(s);

        if (s->hasPredecessor(static_cast<ElemT*>(this)))
            return;
        s->_predecessors.push_back(static_cast<ElemT*>(this));

        assert(hasSuccessor(s));
        assert(s->hasPredecessor(static_cast<ElemT*>(this)));
    }

    void removeSuccessor(ElemT *s) {
        for (unsigned idx = 0; idx < _successors.size(); ++idx) {
            if (_successors[idx] == s) {
                // remove this node from sucessor's predecessors
                auto& preds = s->_predecessors;
                bool found = false;
                for (unsigned idx2 = 0; idx2 < preds.size(); ++idx2) {
                    if (preds[idx2] == this) {
                        found = true;
                        preds[idx2] = preds.back();
                        preds.pop_back();
                        break;
                    }
                }
                assert(!s->hasPredecessor(static_cast<ElemT*>(this)) &&
                        "Did not remove the predecessor");
                assert(found && "Did not find 'this' in predecessors");

                _successors[idx] = _successors.back();
                _successors.pop_back();
                // use the assumption that we have only one successor
                break;
            }
        }

        assert(!hasSuccessor(s) && "Did not remove the successor");
    }

    ElemT *getSinglePredecessor() {
        return _predecessors.size() == 1 ? _predecessors.back() : nullptr;
    }

    ElemT *getSingleSuccessor() {
        return _successors.size() == 1 ? _successors.back() : nullptr;
    }
};

template <typename ElemT>
class CFGElement : public ElemId, public ElemWithEdges<ElemT> { };

template <typename ElemT, typename NodeT>
class BBlockBase : public CFGElement<ElemT> {
    using NodesT = std::list<NodeT *>;

    NodesT _nodes;

public:

    void append(NodeT *n) { _nodes.push_back(n); n->setBBlock(static_cast<ElemT*>(this)); }
    void prepend(NodeT *n) { _nodes.push_front(n); n->setBBlock(static_cast<ElemT*>(this)); }

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
        n->setBBlock(static_cast<ElemT*>(this));
    }

    // FIXME: rename to nodes()
    const NodesT& getNodes() const { return _nodes; }
    NodesT& getNodes() { return _nodes; }
    // FIXME: rename to first/front(), last/back()
    NodeT *getFirst() { return _nodes.empty() ? nullptr : _nodes.front(); }
    NodeT *getLast() { return _nodes.empty() ? nullptr : _nodes.back(); }
    const NodeT *getFirst() const { return _nodes.empty() ? nullptr : _nodes.front(); }
    const NodeT *getLast() const { return _nodes.empty() ? nullptr : _nodes.back(); }

    bool empty() const { return _nodes.empty(); }
    auto size() const -> decltype(_nodes.size()) { return _nodes.size(); }
};

}

#endif
