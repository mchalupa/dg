#ifndef _DG_VALUE_RELATION_ANALYSIS_H_
#define _DG_VALUE_RELATION_ANALYSIS_H_

#include <set>

#ifndef NDEBUG
#include <iostream>
#endif

/// --------------------------------------------------- ///
// Value
/// --------------------------------------------------- ///
enum VRValueType {
    CONSTANT = 1,
    VARIABLE = 2,
    READ     = 3,
};

// XXX: should we use std::variant instead?
class VRValue {
    VRValueType _type;

protected:

    union {
        unsigned id;             // variable
        int64_t constant;        // constant
        VRValue *value{nullptr}; // read
    } _operand;

    VRValue(VRValueType type) : _type(type) {}

public:

#ifndef NDEBUG
    virtual void dump() const { std::cout << "VRValue [" << this << "]"; }
    virtual ~VRValue() = default;
#endif

    bool isRead() const { return _type == VRValueType::READ; }
    bool isConstant() const { return _type == VRValueType::CONSTANT; }
    bool isVariable() const { return _type == VRValueType::VARIABLE; }

    bool operator<(const VRValue& rhs) const {
        if (_type != rhs._type)
            return _type < rhs._type;

        if (isVariable())
            return _operand.id < rhs._operand.id;
        if (isRead())
            return *_operand.value < *rhs._operand.value;
        // constant
        return _operand.constant < rhs._operand.constant;
    }

    bool operator==(const VRValue& rhs) const {
        if (_type != rhs._type)
            return false;

        if (isVariable())
            return _operand.id == rhs._operand.id;
        if (isRead())
            return *_operand.value == *rhs._operand.value;
        // constant
        return _operand.constant == rhs._operand.constant;
    }

    VRValueType getType() const { return _type; }
};

class VRRead : public VRValue {
public:

#ifndef NDEBUG
    void dump() const override {
        std::cout << "Read(";
        _operand.value->dump();
        std::cout << ")";
    }
#endif

    VRRead(VRValue *op) : VRValue(VRValueType::READ) {
        _operand.value = op;
    }
};

class VRVariable : public VRValue {
public:

#ifndef NDEBUG
    void dump() const override {
        std::cout << "V" << _operand.id;
    }
#endif

    VRVariable(unsigned id) : VRValue(VRValueType::VARIABLE) {
        _operand.id = id;
    }
};

class VRConstant : public VRValue {
public:

#ifndef NDEBUG
    void dump() const override {
        std::cout << _operand.constant;
    }
#endif

    VRConstant(int64_t c) : VRValue(VRValueType::CONSTANT) {
        _operand.constant = c;
    }
};

/// --------------------------------------------------- ///
// Relation
/// --------------------------------------------------- ///
class VRRelation {
    enum VRRelationType {
        NONE = 0, EQ = 1, NEQ = 2, LE = 3, LT = 4, GE = 5, GT = 6
    } _relation{VRRelationType::NONE};

    VRValue *_lhs{nullptr}, *_rhs{nullptr};

    VRRelation(VRRelationType type, VRValue *lhs, VRValue *rhs)
    : _relation(type), _lhs(lhs), _rhs(rhs) {}

public:

    // get rid of me!
    VRRelation() = default;

#ifndef NDEBUG
    void dump() const {
        std::cout << "(";
        _lhs->dump();

        switch(_relation) {
        case VRRelationType::EQ: std::cout << " = "; break;
        case VRRelationType::NEQ: std::cout << " != "; break;
        case VRRelationType::LT: std::cout << " < "; break;
        case VRRelationType::LE: std::cout << " <= "; break;
        case VRRelationType::GT: std::cout << " > "; break;
        case VRRelationType::GE: std::cout << " >= "; break;
        default: abort();
        }
        _rhs->dump();
        std::cout << ")";
    }
#endif

    bool operator<(const VRRelation& oth) const {
        if (_relation != oth._relation)
            return _relation < oth._relation;
        return *_lhs == *oth._lhs ?
                *_rhs < *oth._rhs : *_lhs < *oth._lhs;
    }

    bool isEq() const { return _relation == VRRelationType::EQ; }
    bool isNeq() const { return _relation == VRRelationType::NEQ; }
    bool isLt() const { return _relation == VRRelationType::LE; }
    bool isLe() const { return _relation == VRRelationType::LT; }
    bool isGt() const { return _relation == VRRelationType::GT; }
    bool isGe() const { return _relation == VRRelationType::GE; }

    VRValue *getLHS() const { return _lhs; }
    VRValue *getRHS() const { return _rhs; }

    friend struct VREq;
    friend struct VRNeq;
    friend struct VRLt;
    friend struct VRLe;
    friend struct VRGt;
    friend struct VRGe;
};

struct VREq : public VRRelation {
    VREq(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::EQ, lhs, rhs) {}
};

struct VRNeq : public VRRelation {
    VRNeq(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::NEQ, lhs, rhs) {}
};

struct VRLe : public VRRelation {
    VRLe(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::LE, lhs, rhs) {}
};

struct VRLt : public VRRelation {
    VRLt(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::LT, lhs, rhs) {}
};

struct VRGe : public VRRelation {
    VRGe(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::GE, lhs, rhs) {}
};

struct VRGt : public VRRelation {
    VRGt(VRValue *lhs, VRValue *rhs)
    : VRRelation(VRRelationType::GT, lhs, rhs) {}
};


/// --------------------------------------------------- ///
// Relations
/// --------------------------------------------------- ///

class VRRelations {
public: // FIXME: public for now, until we add iterators

    // XXX: if subclasses of VRRelations change size
    // we must use pointers
    using SetT = std::set<VRRelation>;
    SetT eqRelations;
    SetT relations;

public:
    using iterator = SetT::iterator;
    using const_iterator = SetT::const_iterator;

    bool empty() const { return eqRelations.empty() && relations.empty(); }

    bool add(const VRRelation& rel) {
        if (rel.isEq())
            return eqRelations.insert(rel).second;

        return relations.insert(rel).second;
    }

    bool add(const VRRelations& rhs) {
        bool changed = false;
        for (const auto& rel : rhs.eqRelations) {
            changed |= add(rel);
        }
        for (const auto& rel : rhs.relations) {
            changed |= add(rel);
        }
        return changed;
    }

    bool has(const VRRelation& rel) const {
        if (rel.isEq())
            return eqRelations.count(rel) != 0;
        return relations.count(rel) != 0;
    }

    void intersect(const VRRelations& rhs) {
        SetT tmp;
        for (const auto& rel : rhs.eqRelations) {
            if (eqRelations.count(rel) > 0)
                tmp.insert(rel);
        }
        tmp.swap(eqRelations);
        for (const auto& rel : rhs.relations) {
            if (relations.count(rel) > 0)
                tmp.insert(rel);
        }
        tmp.swap(relations);
    }

    /*
    bool forget(const VRRelation& rel) {
        if (rel.isEq())
            return eqRelations.erase(rel) != 0;
        return relations.erase(rel) != 0;
    }

    bool forget(const VRRelations& rels) {
        bool changed = false;
        for (const auto& rel : rels.eqRelations) {
                changed |= eqRelations.erase(rel) != 0;
        }
        for (const auto& rel : rels.relations) {
                changed |= relations.erase(rel) != 0;
        }
        return changed;
    }

    bool forget(const VRValue& val) {
        VRRelations to_erase;
        for (const auto& rel : eqRelations) {
            if (*rel.getLHS() == val ||
                *rel.getRHS() == val)
                to_erase.add(rel);
        }
        for (const auto& rel : relations) {
            if (*rel.getLHS() == val ||
                *rel.getRHS() == val)
                to_erase.add(rel);
        }

        return forget(to_erase);
    }
    */

#ifndef NDEBUG
    void dump() const {
        std::cout << "{";
        bool x = false;
        for (const auto& rel : eqRelations) {
            if (!x) {
                x = true;
            } else
                std::cout << ", ";

            rel.dump();
        }
        for (const auto& rel : relations) {
            if (!x) {
                x = true;
            } else
                std::cout << ", ";

            rel.dump();
        }
        std::cout << "}";
    }
#endif
};

class VRValueSet {
    struct _Cmp {
        bool operator()(const VRValue *a, const VRValue *b) const {
            return *a < *b;
        }
    };

    using SetT = std::set<const VRValue *, _Cmp>;
    SetT values;

public:
    using iterator = SetT::iterator;
    using const_iterator = SetT::const_iterator;

    bool empty() const { return values.empty(); }
    size_t size() const { return values.size(); }
    size_t count(const VRValue *v) const { return values.count(v); }

    bool add(const VRValue *v) { return values.insert(v).second; }
    template <typename InputIterator>
    bool add(InputIterator it, InputIterator et) {
        bool changed = false;
        for (; it != et; ++it)
            changed |= add(*it);
        return changed;
    }

    iterator begin() { return values.begin(); }
    iterator end() { return values.end(); }
    const_iterator begin() const { return values.begin(); }
    const_iterator end() const { return values.end(); }
};


/// --------------------------------------------------- ///
// VRInfo
// -- information about generated/forgotten relations
/// --------------------------------------------------- ///

class VRInfo {
    // generated or forgetten relations
    VRRelations _gen;

    // forget concrete relations
    VRRelations _forget;
    // forget all relations with these values
    VRValueSet _forget_with;

    bool _forget_all{false};
    bool _forget_all_reads{false};

public:
    void addGen(const VRRelation& rel) {
        _gen.add(rel);
    }

    void addGen(const VRRelations& rels) {
        _gen.add(rels);
    }

    const VRRelations& generates() const { return _gen; }

    void addForget(const VRRelation& rel) {
        _forget.add(rel);
    }

    void addForget(const VRValue *val) {
        _forget_with.add(val);
    }

    void addForgetAll() { _forget_all = true; }
    void addForgetAllReads() { _forget_all_reads = true; }

    void add(const VRInfo& rhs) {
        _forget_all |= rhs._forget_all;
        _forget_all_reads |= rhs._forget_all_reads;
        _gen.add(rhs._gen);
        _forget.add(rhs._forget);
        _forget_with.add(rhs._forget_with.begin(),
                         rhs._forget_with.end());
    }

    bool forgets(const VRRelation& rel) {
        if (_forget_all)
            return true;
        if (_forget_all_reads) {
            if (rel.getLHS()->isRead() ||
                rel.getRHS()->isRead())
            return true;
        }

        return _gen.has(rel) ||
               _forget_with.count(rel.getLHS()) > 0 ||
               _forget_with.count(rel.getRHS()) > 0;
    }

#ifndef NDEBUG
    void dump() const {
        if (!_gen.empty()) {
            std::cout << "GEN ";
            _gen.dump();
        }

        if (_forget.empty() && _forget_with.empty()
            && !_forget_all && !_forget_all_reads)
            return;

        std::cout << " FORGETS ";
        if (_forget_all)
            std::cout << "all ";
        if (_forget_all_reads)
            std::cout << "all reads ";
        if (!_forget.empty())
            _forget.dump();
        if (_forget_with.empty())
            return;
        std::cout << " all with {";
        bool x = false;
        for (const auto& val : _forget_with) {
            if (!x) {
                x = true;
            } else
                std::cout << ", ";

            val->dump();
        }
        std::cout << "}";
    }
#endif
};

#endif // _DG_VALUE_RELATION_ANALYSIS_H_
