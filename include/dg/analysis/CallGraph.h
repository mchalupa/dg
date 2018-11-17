#ifndef _DG_GENERIC_CALLGRAPH_H_
#define _DG_GENERIC_CALLGRAPH_H_

#include <map>
#include <vector>

template <typename ValueT>
class GenericCallGraph {
public:
    class FuncNode {
        unsigned _id;
        std::vector<FuncNode *> _calls;
        std::vector<FuncNode *> _callers;

        template <typename Cont>
        bool _contains(FuncNode *x, const Cont& C) const {
            for (auto s : C) {
                if (s == x)
                    return true;
            }
            return false;
        }

    public:
        ValueT value;

        FuncNode(unsigned id, ValueT& nd) : _id(id), value(nd) {};
        FuncNode(FuncNode&&) = default;

        bool calls(FuncNode *x) const { return _contains(x, _calls); }
        bool isCalledBy(FuncNode *x) const { return _contains(x, _callers); }
        unsigned getID() const { return _id; }

        bool addCall(FuncNode *x) {
            if (calls(x))
                return false;
            _calls.push_back(x);
            if (!x->isCalledBy(this))
                x->_callers.push_back(this);
            return true;
        }

        const std::vector<FuncNode *>& getCalls() const { return _calls; }
        const std::vector<FuncNode *>& getCallers() const { return _callers; }
    };

    // a calls b
    bool addCall(ValueT& a, ValueT& b) {
        auto A = getOrCreate(a);
        auto B = getOrCreate(b);
        return A->addCall(B);
    }

private:
    unsigned last_id{0};

    FuncNode *getOrCreate(ValueT& v) {
        auto it = _mapping.find(v);
        if (it == _mapping.end()) {
            auto newIt = _mapping.emplace(v, FuncNode(++last_id, v));
            return &newIt.first->second;
        }
        return &it->second;
    }

    std::map<ValueT, FuncNode> _mapping;
    //std::map<ValueT, FuncNode *> _mapping;
    //std::vector<FuncNode> _nodes;

public:
    auto begin() -> decltype(_mapping.begin()) { return _mapping.begin(); }
    auto end() -> decltype(_mapping.end()) { return _mapping.end(); }
    auto begin() const -> decltype(_mapping.begin()) { return _mapping.begin(); }
    auto end() const -> decltype(_mapping.end()) { return _mapping.end(); }
};

#endif // _DG_GENERIC_CALLGRAPH_H_
