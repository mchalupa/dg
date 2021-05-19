#ifndef DG_GENERIC_CALLGRAPH_H_
#define DG_GENERIC_CALLGRAPH_H_

#include <map>
#include <vector>

#include "dg/util/iterators.h"

namespace dg {

template <typename ValueT>
class GenericCallGraph {
  public:
    class FuncNode {
        unsigned _id;
        unsigned _scc_id{0};
        std::vector<FuncNode *> _calls;
        std::vector<FuncNode *> _callers;

        template <typename Cont>
        bool _contains(const FuncNode *x, const Cont &C) const {
            return dg::any_of(C, [x](FuncNode *s) { return s == x; });
        }

      public:
        const ValueT value;

        FuncNode(unsigned id, const ValueT &nd) : _id(id), value(nd){};
        FuncNode(FuncNode &&) = default;

        bool calls(const FuncNode *x) const { return _contains(x, _calls); }
        bool isCalledBy(FuncNode *x) const { return _contains(x, _callers); }

        unsigned getID() const { return _id; }
        unsigned getSCCId() const { return _scc_id; }
        void setSCCId(unsigned id) { _scc_id = id; }

        bool addCall(FuncNode *x) {
            if (calls(x))
                return false;
            _calls.push_back(x);
            if (!x->isCalledBy(this))
                x->_callers.push_back(this);
            return true;
        }

        const std::vector<FuncNode *> &getCalls() const { return _calls; }
        // alias for getCalls()
        const std::vector<FuncNode *> &successors() const { return getCalls(); }
        const std::vector<FuncNode *> &getCallers() const { return _callers; }

        const ValueT &getValue() const { return value; };
    };

  private:
    unsigned last_id{0};

    FuncNode *getOrCreate(const ValueT &v) {
        auto it = _mapping.find(v);
        if (it == _mapping.end()) {
            auto newIt = _mapping.emplace(v, FuncNode(++last_id, v));
            return &newIt.first->second;
        }
        return &it->second;
    }

    std::map<const ValueT, FuncNode> _mapping;

  public:
    // just create a node for the value
    // (e.g., the entry node)
    FuncNode *createNode(const ValueT &a) { return getOrCreate(a); }

    // a calls b
    bool addCall(const ValueT &a, const ValueT &b) {
        auto A = getOrCreate(a);
        auto B = getOrCreate(b);
        return A->addCall(B);
    }

    const FuncNode *get(const ValueT &v) const {
        auto it = _mapping.find(v);
        if (it == _mapping.end()) {
            return nullptr;
        }
        return &it->second;
    }

    FuncNode *get(const ValueT &v) {
        auto it = _mapping.find(v);
        if (it == _mapping.end()) {
            return nullptr;
        }
        return &it->second;
    }

    bool empty() const { return _mapping.empty(); }

    auto begin() -> decltype(_mapping.begin()) { return _mapping.begin(); }
    auto end() -> decltype(_mapping.end()) { return _mapping.end(); }
    auto begin() const -> decltype(_mapping.begin()) {
        return _mapping.begin();
    }
    auto end() const -> decltype(_mapping.end()) { return _mapping.end(); }
};

} // namespace dg

#endif
