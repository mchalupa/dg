#ifndef DG_LLVM_VALUE_RELATIONS_RELATIONS_H
#define DG_LLVM_VALUE_RELATIONS_RELATIONS_H

#ifndef NDEBUG
#include <iostream>
#endif

#include <array>
#include <bitset>

namespace dg {
namespace vr {

struct Relations {
    // ************** type stuff **************
    enum Type { EQ, NE, LE, LT, GE, GT, PT, PF };
    static const size_t total = 8;
    static const std::array<Type, total> all;

    static Type inverted(Type type);
    static Type negated(Type type);
    static bool transitiveOver(Type one, Type two);
    static Relations conflicting(Type type);
    static bool isStrict(Type type);
    static bool isNonStrict(Type type);
    static Type getStrict(Type type);
    static Type getNonStrict(Type type);
    static Relations getAugmented(Relations rels);

    // ************** bitset stuff **************
    Relations() = default;
    explicit Relations(unsigned long long val) : bits(val) {}

    bool has(Type type) const { return bits[type]; }
    Relations &set(Type t, bool v = true) {
        bits.set(t, v);
        return *this;
    }

    Type get() const;

    Relations &eq() { return set(EQ); }
    Relations &ne() { return set(NE); }
    Relations &le() { return set(LE); }
    Relations &lt() { return set(LT); }
    Relations &ge() { return set(GE); }
    Relations &gt() { return set(GT); }
    Relations &pt() { return set(PT); }
    Relations &pf() { return set(PF); }

    Relations &addImplied();
    Relations &invert();
    bool conflictsWith(Type type) const { return anyCommon(conflicting(type)); }
    bool anyCommon(const Relations &other) const {
        return (bits & other.bits).any();
    }

    friend bool operator==(const Relations &lt, const Relations &rt) {
        return lt.bits == rt.bits;
    }
    friend bool operator!=(const Relations &lt, const Relations &rt) {
        return !(lt == rt);
    }

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out, const Relations &rels);
#endif

  private:
    std::bitset<total> bits;
};

const Relations allRelations(~0);
const Relations comparative(1 << Relations::NE | 1 << Relations::LT |
                            1 << Relations::LE | 1 << Relations::GT |
                            1 << Relations::GE);

#ifndef NDEBUG
std::ostream &operator<<(std::ostream &out, Relations::Type r);
#endif

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATIONS_H