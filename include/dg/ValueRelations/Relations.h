#ifndef DG_LLVM_VALUE_RELATIONS_RELATIONS_H_
#define DG_LLVM_VALUE_RELATIONS_RELATIONS_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <array>
#include <bitset>

namespace dg {
namespace vr {

struct Relations {
    // ************** type stuff **************
    enum Type { EQ, NE, SLE, SLT, ULE, ULT, SGE, SGT, UGE, UGT, PT, PF };
    static const size_t total = 12;
    static const std::array<Type, total> all;

    static Type inverted(Type type);
    static Type negated(Type type);
    static bool transitiveOver(Type fst, Type snd);
    static Relations conflicting(Type type);
    static bool isStrict(Type type);
    static bool isNonStrict(Type type);
    static Type getStrict(Type type);
    static Type getNonStrict(Type type);
    static Relations getAugmented(Relations rels);
    static bool isSigned(Type type);

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
    Relations &sle() { return set(SLE); }
    Relations &slt() { return set(SLT); }
    Relations &ule() { return set(ULE); }
    Relations &ult() { return set(ULT); }
    Relations &sge() { return set(SGE); }
    Relations &sgt() { return set(SGT); }
    Relations &uge() { return set(UGE); }
    Relations &ugt() { return set(UGT); }
    Relations &pt() { return set(PT); }
    Relations &pf() { return set(PF); }

    Relations &addImplied();
    Relations &invert();
    bool any() const { return bits.any(); }
    bool conflictsWith(Type type) const { return anyCommon(conflicting(type)); }
    bool anyCommon(const Relations &other) const {
        return (bits & other.bits).any();
    }
    /* if A lt B and B rt C, return relations between A and C */
    friend Relations compose(const Relations &lt, const Relations &rt);

    friend bool operator==(const Relations &lt, const Relations &rt) {
        return lt.bits == rt.bits;
    }
    friend bool operator!=(const Relations &lt, const Relations &rt) {
        return !(lt == rt);
    }

    Relations &operator&=(const Relations &other) {
        bits &= other.bits;
        return *this;
    }
    Relations &operator|=(const Relations &other) {
        bits |= other.bits;
        return *this;
    }
    friend Relations operator&(const Relations &lt, const Relations &rt) {
        return Relations((lt.bits & rt.bits).to_ulong());
    }
    friend Relations operator|(const Relations &lt, const Relations &rt) {
        return Relations((lt.bits | rt.bits).to_ulong());
    }

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out, const Relations &rels);
#endif

  private:
    std::bitset<total> bits;
};

const Relations allRelations(~0);
const Relations comparative(1 << Relations::EQ | 1 << Relations::NE |
                            1 << Relations::SLT | 1 << Relations::SLE |
                            1 << Relations::ULT | 1 << Relations::ULE |
                            1 << Relations::SGT | 1 << Relations::SGE |
                            1 << Relations::UGT | 1 << Relations::UGE);
const Relations restricted(1 << Relations::EQ | 1 << Relations::NE |
                           1 << Relations::SLT | 1 << Relations::SLE |
                           1 << Relations::ULT | 1 << Relations::ULE |
                           1 << Relations::PT);
const Relations strict(1 << Relations::SLT | 1 << Relations::ULT |
                       1 << Relations::SGT | 1 << Relations::UGT);
const Relations nonStrict(1 << Relations::SLE | 1 << Relations::ULE |
                          1 << Relations::SGE | 1 << Relations::UGE);

#ifndef NDEBUG
std::ostream &operator<<(std::ostream &out, Relations::Type r);
#endif

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATIONS_H_
