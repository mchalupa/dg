#ifndef _DG_LLVM_EQUALITY_MAP_H_
#define _DG_LLVM_EQUALITY_MAP_H_

#include <memory>
#include <cassert>

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace llvm {
    class Value;
}

namespace dg {

template <typename T>
class EqualityMap {
    struct _Cmp {
        bool operator()(const llvm::Value *a, const llvm::Value *b) const {
            // XXX: merge constants?
            return a < b;
        }
    };

    using SetT = std::set<T, _Cmp>;
    using ClassT = std::shared_ptr<SetT>;
    std::map<T, ClassT, _Cmp> _map;

    // FIXME: use variadic templates
    ClassT newClass(const T& a, const T& b) {
        auto cls = ClassT(new SetT());
        cls->insert(a);
        cls->insert(b);
        return cls;
    }

    ClassT newClass(const T& a) {
        auto cls = ClassT(new SetT());
        cls->insert(a);
        return cls;
    }

public:
    bool add(const T& a, const T& b) {
        auto itA = _map.find(a);
        auto itB = _map.find(b);
        if (itA == _map.end()) {
            if (itB == _map.end()) {
                if (a == b) {
                    auto newcls = newClass(a);
                    _map[a] = newcls;
                    assert(newcls.use_count() == 2);
                } else {
                    auto newcls = newClass(a, b);
                    _map[b] = newcls;
                    _map[a] = newcls;
                    assert(newcls.use_count() == 3);
                }
            } else {
                auto B = itB->second;
                B->insert(a);
                _map[a] = B;
            }
        } else {
            auto A = itA->second;
            if (itB == _map.end()) {
                A->insert(b);
                _map[b] = A;
            } else {
                // merge of classes
                auto B = itB->second;
                if (A == B)
                    return false;

                for (auto& val : *B.get()) {
                    A->insert(val);
                    _map[val] = A;
                }
                assert(B.use_count() == 1);
                A->insert(b);
                assert(_map[b] == A);
            }
        }

        assert(!_map.empty());
        assert(get(a) != nullptr);
        assert(get(a) == get(b));
        assert(get(a)->count(a) > 0);
        assert(get(a)->count(b) > 0);
        return true;
    }

    bool add(const EqualityMap& rhs) {
        bool changed = false;
        // FIXME: not very efficient
        for (auto& it : rhs._map) {
            for (auto& eq : *it.second.get()) {
                changed |= add(it.first, eq);
            }
        }

        return changed;
    }

    bool add(const T& a, const SetT& S) {
        bool changed = false;
        for (auto eq : S) {
            changed |= add(a, eq);
        }

        return changed;
    }

    SetT *get(const T& a) {
        auto it = _map.find(a);
        if (it == _map.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    SetT *get(const T& a) const {
        auto it = _map.find(a);
        if (it == _map.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    void intersect(const EqualityMap& rhs) {
        EqualityMap tmp;
        for (auto& it : rhs) {
            auto ourS = get(it.first);
            if (!ourS)
                continue;

            for (auto x : *ourS) {
                if (it.second->count(x) > 0)
                    tmp.add(it.first, x);
            }
        }

        _map.swap(tmp._map);
    }

    auto begin() -> decltype(_map.begin()) { return _map.begin(); }
    auto end() -> decltype(_map.end()) { return _map.end(); }
    auto begin() const -> decltype(_map.begin()) { return _map.begin(); }
    auto end() const -> decltype(_map.end()) { return _map.end(); }

#ifndef NDEBUG
    void dump() const {
        std::set<SetT*> classes;
        for (const auto& it : _map) {
            classes.insert(it.second.get());
        }

        if (classes.empty()) {
            return;
        }

        for (const auto cls : classes) {
            std::cout << "{";
            int t = 0;
            for (const auto& val : *cls) {
                if (++t > 1)
                    std::cout << " = ";
                std::cout << debug::getValName(val);
            }
            std::cout << "}\n";
        }
        std::cout << std::endl;
    }
#endif
};


} // namespace dg
#endif // _DG_LLVM_EQUALITY_MAP_H_
