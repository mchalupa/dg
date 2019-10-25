#ifndef _DG_LLVM_RELATIONS_H_
#define _DG_LLVM_RELATIONS_H_

#include <memory>
#include <cassert>

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace llvm {
    class Value;
}

namespace dg {

/// --------------------------------------------------- ///
// Relation
/// --------------------------------------------------- ///
class Relations;

enum VRRelationType {
    NONE = 0, EQ = 1, NEQ = 2, LE = 3, LT = 4, GE = 5, GT = 6
};

class VRRelation {
    VRRelationType _relation{VRRelationType::NONE};

    const llvm::Value *_lhs{nullptr}, *_rhs{nullptr};

    VRRelation(VRRelationType type,
               const llvm::Value *lhs, const llvm::Value *rhs)
    : _relation(type), _lhs(lhs), _rhs(rhs) {
        // this can of course happen when we add a spurious
        // constraints, but now we have it to catch bugs
        assert((!isLt() && !isGt()) || lhs != rhs);
    }

    auto getRelation() const -> decltype(_relation) { return _relation; }
    bool isNone() const { return _relation == VRRelationType::NONE; }

public:

	VRRelation() = default;

#ifndef NDEBUG
    void dump() const {
        std::cout << "(" << debug::getValName(_lhs);

        switch(_relation) {
        case VRRelationType::EQ: std::cout << " = "; break;
        case VRRelationType::NEQ: std::cout << " != "; break;
        case VRRelationType::LT: std::cout << " < "; break;
        case VRRelationType::LE: std::cout << " <= "; break;
        case VRRelationType::GT: std::cout << " > "; break;
        case VRRelationType::GE: std::cout << " >= "; break;
        default: abort();
        }
        std::cout << debug::getValName(_rhs) << ")";
    }
#endif

    // check whether the relation is properly initialized
    // (it was not created by the default constructor)
    operator bool() const { return !isNone(); }

    bool operator<(const VRRelation& oth) const {
        return _lhs == oth._lhs ?
                _rhs < oth._rhs : _lhs < oth._lhs;
    }

    bool isEq() const { return _relation == VRRelationType::EQ; }
    bool isNeq() const { return _relation == VRRelationType::NEQ; }
    bool isLt() const { return _relation == VRRelationType::LT; }
    bool isLe() const { return _relation == VRRelationType::LE; }
    bool isGt() const { return _relation == VRRelationType::GT; }
    bool isGe() const { return _relation == VRRelationType::GE; }

    auto getLHS() const -> decltype(_lhs) { return _lhs; }
    auto getRHS() const -> decltype(_rhs) { return _rhs; }

	static VRRelation Eq(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::EQ, l, r);
	}
	static VRRelation Neq(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::NEQ, l, r);
	}
	static VRRelation Lt(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::LT, l, r);
	}
	static VRRelation Le(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::LE, l, r);
	}
	static VRRelation Gt(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::GT, l, r);
	}
	static VRRelation Ge(const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(VRRelationType::GE, l, r);
	}
	static VRRelation sameOp(const VRRelation& rel, const llvm::Value *l, const llvm::Value *r) {
		return VRRelation(rel._relation, l, r);
	}

	static VRRelation Not(const VRRelation& rel) {
        switch(rel._relation) {
        case VRRelationType::EQ:  return Neq(rel._lhs, rel._rhs);
        case VRRelationType::NEQ: return Eq(rel._lhs, rel._rhs);
        case VRRelationType::LT:  return Ge(rel._lhs, rel._rhs);
        case VRRelationType::LE:  return Gt(rel._lhs, rel._rhs);
        case VRRelationType::GT:  return Le(rel._lhs, rel._rhs);
        case VRRelationType::GE:  return Lt(rel._lhs, rel._rhs);
        default: abort();
        }
	}

	static VRRelation reverse(const VRRelation& rel) {
        switch(rel._relation) {
        case VRRelationType::EQ:  return Eq(rel._rhs, rel._lhs);
        case VRRelationType::NEQ: return Neq(rel._rhs, rel._lhs);
        case VRRelationType::LT:  return Gt(rel._rhs, rel._lhs);
        case VRRelationType::LE:  return Ge(rel._rhs, rel._lhs);
        case VRRelationType::GT:  return Lt(rel._rhs, rel._lhs);
        case VRRelationType::GE:  return Le(rel._rhs, rel._lhs);
        default: abort();
        }
	}

	friend VRRelation Eq(const llvm::Value *, const llvm::Value *);
	friend VRRelation Neq(const llvm::Value *, const llvm::Value *);
	friend VRRelation Lt(const llvm::Value *, const llvm::Value *);
	friend VRRelation Le(const llvm::Value *, const llvm::Value *);
	friend VRRelation Gt(const llvm::Value *, const llvm::Value *);
	friend VRRelation Ge(const llvm::Value *, const llvm::Value *);
	friend VRRelation Not(const VRRelation& r);
	friend VRRelation reverse(const VRRelation&);
	friend VRRelation sameOp(const VRRelation&, const llvm::Value *, const llvm::Value *);
    friend class Relations;
};

/// Set of relations for one value
class Relations {
    // this value is in relation with values in rhs
    const llvm::Value *value;
    // one set for each type of relation
    std::set<const llvm::Value *> rhs[7]{{}};

public:
    Relations(const llvm::Value *v) : value(v) {}

    bool add(const VRRelation& rel) {
        assert(rel.getRelation() != VRRelationType::NONE);
        assert(rel.getLHS() == value);
        return rhs[rel.getRelation()].insert(rel.getRHS()).second;
    }

    bool add(const Relations& oth) {
        bool changed = false;
        for (int i = 0; i < 7; ++i) {
            for (auto& it : oth.rhs[i])
                changed |= rhs[i].insert(it).second;
        }

        return changed;
    }

    bool has(VRRelationType t, const llvm::Value *x) const {
        assert(t > 0 && t < 7);
        return rhs[t].count(x) > 0;
    }

    bool has(const VRRelation& rel) const {
        assert(rel.getLHS() == value);
        return has(rel.getRelation(), rel.getRHS());
    }

    struct const_iterator {
        unsigned idx;
        const Relations& relations;
        std::set<const llvm::Value *>::const_iterator it;

        const_iterator(const Relations& r, unsigned idx)
        : idx(idx), relations(r) {
            if (idx < 7) {
                it = relations.rhs[idx].begin();
                _get_next();
            }
        }

        VRRelation operator*() const {
            return VRRelation(static_cast<VRRelationType>(idx),
                              relations.value, *it);
        }

        const_iterator& operator++() {
            ++it;
            _get_next();
            return *this;
        }

        bool operator==(const const_iterator& rhs) const {
            if (idx < 7) {
                return idx == rhs.idx ? it == rhs.it : false;
            }

            return idx == rhs.idx;
        }

        bool operator!=(const const_iterator& rhs) const { return !operator==(rhs); }
    private:
        void _get_next() {
            while (idx < 7) {
                if (it == relations.rhs[idx].end())
                    ++idx;
                else
                    break;
                if (idx >= 7)
                    break;
                it = relations.rhs[idx].begin();
            }
        }
    };

    const_iterator begin() const { return const_iterator(*this, 1); }
    const_iterator end() const { return const_iterator(*this, 7); }


#ifndef NDEBUG
    void dump() const {
        for (int i = 1; i < 7; ++i) {
            for (const auto& r : rhs[i]) {
                VRRelation(static_cast<VRRelationType>(i),
                           value, r).dump();
                std::cout << "\n";
            }
        }
    }
#endif // NDEBUG

};

class RelationsMap {
    // with each relation, add also all relations that follow from transitivity.
    // NOTE: this may cause big overhead
    bool _keep_transitively_closed{false};
    std::map<const llvm::Value *, Relations> relations;

    bool addTransitiveEq(const VRRelation& rel) {
        bool changed = false;
        if (auto B = get(rel.getRHS())) {
            auto tmp = *B; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getRHS());
                changed |= add(VRRelation::sameOp(it, rel.getLHS(), it.getRHS()));
            }
        }
        if (auto A = get(rel.getLHS())) {
            auto tmp = *A; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getLHS());
                changed |= add(VRRelation::sameOp(it, rel.getRHS(), it.getRHS()));
            }
        }
        return changed;
    }

    bool addTransitiveLt(const VRRelation& rel) {
        bool changed = false;
        if (auto B = get(rel.getRHS())) {
            auto tmp = *B; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getRHS());
                if (it.isEq() || it.isLt() || it.isLe())
                    changed |= add(VRRelation::sameOp(rel, rel.getLHS(), it.getRHS()));
            }
        }
        if (auto A = get(rel.getLHS())) {
            auto tmp = *A; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getLHS());
                if (it.isEq() || it.isGt() || it.isGe())
                    changed |= add(VRRelation::sameOp(rel, it.getRHS(), rel.getRHS()));
            }
        }
        return changed;
    }

    bool addTransitiveLe(const VRRelation& rel) {
        bool changed = false;
        if (auto B = get(rel.getRHS())) {
            auto tmp = *B; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getRHS());
                if (it.isEq() || it.isLe())
                    changed |= add(VRRelation::sameOp(rel, rel.getLHS(), it.getRHS()));
                else if (it.isLt())
                    changed |= add(VRRelation::sameOp(it, rel.getLHS(), it.getRHS()));
            }
        }
        if (auto A = get(rel.getLHS())) {
            auto tmp = *A; // we would modify iterator when iterating over *B
            for (const auto& it : tmp) {
                assert(it.getLHS() == rel.getLHS());
                if (it.isEq() || it.isGe())
                    changed |= add(VRRelation::sameOp(rel, it.getRHS(), rel.getRHS()));
                else if (it.isGt())
                    changed |= add(VRRelation::sameOp(it, it.getRHS(), rel.getRHS()));
            }
        }
        return changed;
    }

    bool _addTransitive1(const VRRelation& rel) {
        bool changed = false;
        if (rel.isEq()) {
            changed |= addTransitiveEq(rel);
        } else if (rel.isLt()) {
            changed |= addTransitiveLt(rel);
        } else if (rel.isLe()) {
            changed |= addTransitiveLe(rel);
        } else if (rel.isGt()) {
            changed |= addTransitiveLt(VRRelation::reverse(rel));
        } else if (rel.isGe()) {
            changed |= addTransitiveLe(VRRelation::reverse(rel));
        }

        return changed;
    }

    bool _addTransitive(const VRRelation& rel) {
        bool changed, changed_any = false;
        do {
            changed = false;
            changed |= _addTransitive1(rel);
            if (!rel.isEq() && !rel.isNeq()) // these are symmetric, all work has been done
                changed |= _addTransitive1(VRRelation::reverse(rel));
            changed_any |= changed;
        } while(changed);

        return changed_any;
    }

    bool _add(const VRRelation& rel) {
        auto it = relations.find(rel.getLHS());
        if (it == relations.end()) {
            auto n = relations.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(rel.getLHS()),
                                       std::forward_as_tuple(rel.getLHS()));
            return n.first->second.add(rel);
        } else {
            return it->second.add(rel);
        }
    }

public:
    RelationsMap(bool keep_trans = false) : _keep_transitively_closed(keep_trans), relations{} {}
    RelationsMap(const RelationsMap&) = default;

    bool add(const VRRelation& rel) {
        bool ret = _add(rel);
        if (_keep_transitively_closed) // we want also the reverse mapping here
            ret |= _add(VRRelation::reverse(rel));

        if (ret && _keep_transitively_closed)
            _addTransitive(rel);

        return ret;
    }

    bool add(const RelationsMap& rhs) {
        bool changed = false;

        for (const auto& it : rhs) {
            for (const auto& rel : it.second) {
                changed |= add(rel);
            }
        }
        return changed;
    }

    bool has(const VRRelation& rel) const {
        auto it = relations.find(rel.getLHS());
        if (it == relations.end())
            return false;
        return it->second.has(rel);
    }

    Relations *get(const llvm::Value *v) {
        auto it = relations.find(v);
        if (it == relations.end()) {
            return nullptr;
        }
        return &it->second;
    }

    void transitivelyClose() {
        auto tmp = relations;
        for (auto& it : tmp) {
            for (const auto& r : it.second) {
                // add mapping also for right-hand sides of relations
                add(VRRelation::reverse(r));
            }
        }
        bool changed;
        do {
            changed = false;
            auto tmp = relations;
            for (auto& it : tmp) {
                for (const auto& r : it.second) {
                    changed |= _addTransitive(r);
                }
            }
        } while (changed);
    }

    /*
	void intersect(const RelationsMap& rhs) {
		decltype(relations) tmp;
		for (const auto& r : rhs) {
			if (relations.count(r) > 0)
				tmp.insert(r);
		}
		relations.swap(tmp);
	}
    */

    auto begin() -> decltype(relations.begin()) { return relations.begin(); }
    auto end() -> decltype(relations.end()) { return relations.end(); }
    auto begin() const -> decltype(relations.begin()) { return relations.begin(); }
    auto end() const -> decltype(relations.end()) { return relations.end(); }

#ifndef NDEBUG
    void dump() const {
        std::cout << "{";
        for (auto& it : relations)
            it.second.dump();
        std::cout << "}";
    }
#endif // NDEBUG
};

} // namespace dg

#endif // _DG_LLVM_RELATIONS_H_
