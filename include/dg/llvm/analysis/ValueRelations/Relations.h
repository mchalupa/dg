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
class VRRelation {
    enum VRRelationType {
        NONE = 0, EQ = 1, NEQ = 2, LE = 3, LT = 4, GE = 5, GT = 6
    } _relation{VRRelationType::NONE};

    const llvm::Value *_lhs{nullptr}, *_rhs{nullptr};

    VRRelation(VRRelationType type,
               const llvm::Value *lhs, const llvm::Value *rhs)
    : _relation(type), _lhs(lhs), _rhs(rhs) {}

    auto getRelation() const -> decltype(_relation) { return _relation; }

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

    bool operator<(const VRRelation& oth) const {
        return _lhs == oth._lhs ?
                _rhs < oth._rhs : _lhs < oth._lhs;
    }

    bool isEq() const { return _relation == VRRelationType::EQ; }
    bool isNeq() const { return _relation == VRRelationType::NEQ; }
    bool isLt() const { return _relation == VRRelationType::LE; }
    bool isLe() const { return _relation == VRRelationType::LT; }
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

	static VRRelation revert(const VRRelation& rel) {
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
	friend VRRelation revert(const VRRelation&);
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
        assert(rel.getRelation() != VRRelation::VRRelationType::NONE);
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
            return VRRelation(static_cast<VRRelation::VRRelationType>(idx),
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
                VRRelation(static_cast<VRRelation::VRRelationType>(i),
                           value, r).dump();
            }
        }
    }
#endif // NDEBUG

};

class RelationsMap {
    std::map<const llvm::Value *, Relations> relations;

public:
    RelationsMap() = default;
    RelationsMap(const RelationsMap&) = default;

    bool add(const VRRelation& rel) {
        auto it = relations.find(rel.getLHS());
        if (it == relations.end()) {
            auto n = relations.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(rel.getLHS()),
                                       std::forward_as_tuple(rel.getLHS()));
            return n.first->second.add(rel);
        }

        return it->second.add(rel);
    }

	bool add(const RelationsMap& rhs) {
		bool changed = false;
		for (const auto& it : rhs) {
            auto my = relations.find(it.first);
            if (my == relations.end()) {
                auto myNew = relations.emplace(std::piecewise_construct,
                                               std::forward_as_tuple(it.first),
                                               std::forward_as_tuple(it.first));
                changed |= myNew.first->second.add(it.second);
                continue;
            }
			changed |= my->second.add(it.second);
		}
		return changed;
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
