#ifndef DG_BBLOCK_BASE_H_
#define DG_BBLOCK_BASE_H_

#include <vector>

namespace dg {

class BBlockId {
    static unsigned idcnt;
    unsigned id;
public:
    BBlockId() : id(++idcnt) {}
    unsigned getID() const { return id; }
};

template <typename BBlockT>
class BBlockBase : public BBlockId {
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

    const EdgesT successors() const { return _successors; }
    const EdgesT predecessors() const { return _predecessors; }

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

}

#endif
