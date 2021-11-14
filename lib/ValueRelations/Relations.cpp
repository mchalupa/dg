#include "dg/ValueRelations/Relations.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace dg {
namespace vr {

const std::array<Relations::Type, Relations::total> Relations::all = {
        EQ, NE, SLE, SLT, ULE, ULT, SGE, SGT, UGE, UGT, PT, PF};

Relations::Type Relations::inverted(Relations::Type type) {
    switch (type) {
    case EQ:
        return EQ;
    case NE:
        return NE;
    case SLE:
        return SGE;
    case SLT:
        return SGT;
    case ULE:
        return UGE;
    case ULT:
        return UGT;
    case SGE:
        return SLE;
    case SGT:
        return SLT;
    case UGE:
        return ULE;
    case UGT:
        return ULT;
    case PT:
        return PF;
    case PF:
        return PT;
    }
    assert(0 && "unreachable");
    abort();
}

Relations::Type Relations::negated(Type type) {
    switch (type) {
    case EQ:
        return NE;
    case NE:
        return EQ;
    case SLE:
        return SGT;
    case SLT:
        return SGE;
    case ULE:
        return UGT;
    case ULT:
        return UGE;
    case SGE:
        return SLT;
    case SGT:
        return SLE;
    case UGE:
        return ULT;
    case UGT:
        return ULE;
    case PT:
    case PF:
        break;
    }
    assert(0 && "no negation for relation");
    abort();
}

Relations Relations::conflicting(Relations::Type type) {
    switch (type) {
    case EQ:
        return Relations().ne().slt().ult().sgt().ugt();
    case NE:
        return Relations().eq();
    case SLT:
        return Relations().eq().sgt().sge();
    case ULT:
        return Relations().eq().ugt().uge();
    case SGT:
        return Relations().eq().slt().sle();
    case UGT:
        return Relations().eq().ult().ule();
    case SLE:
        return Relations().sgt();
    case ULE:
        return Relations().ugt();
    case SGE:
        return Relations().slt();
    case UGE:
        return Relations().ult();
    case PT:
    case PF:
        return {};
    }
    assert(0 && "unreachable");
    abort();
}

Relations::Type Relations::get() const {
    for (Type rel : {EQ, SLT, ULT, SGT, UGT, NE, SLE, ULE, SGE, UGE, PT, PF}) {
        if (has(rel))
            return rel;
    }
    assert(0 && "unreachable");
    abort();
}

Relations &Relations::addImplied() {
    if (has(EQ))
        sle().ule().sge().uge();
    if (has(SLT))
        sle().ne();
    if (has(ULT))
        ule().ne();
    if (has(SGT))
        sge().ne();
    if (has(UGT))
        uge().ne();
    return *this;
}

Relations &Relations::invert() {
    std::bitset<Relations::total> newBits;
    for (Type rel : Relations::all) {
        if (has(rel)) {
            newBits.set(inverted(rel));
        }
    }

    std::swap(bits, newBits);
    return *this;
}

Relations Relations::getAugmented(Relations rels) {
    Relations augmented = rels;
    for (Relations::Type type : Relations::all) {
        if (!strict.has(type))
            continue;

        if (augmented.has(type)) {
            augmented.set(Relations::getNonStrict(type));
        } else if (augmented.has(Relations::getNonStrict(type))) {
            augmented.set(type);
            augmented.eq();
        }
    }
    return augmented;
}

bool Relations::isSigned(Type type) {
    switch (type) {
    case SLT:
    case SLE:
    case SGT:
    case SGE:
        return true;
    case ULT:
    case ULE:
    case UGT:
    case UGE:
        return false;
    default:
        assert(0 && "unreachable");
        abort();
    }
}

Relations compose(const Relations &lt, const Relations &rt) {
    if (lt.has(Relations::EQ))
        return rt;
    if (rt.has(Relations::EQ))
        return lt;
    Relations result;
    for (Relations::Type ltRel : Relations::all) {
        if (!lt.has(ltRel))
            continue;

        for (Relations::Type rtRel : Relations::all) {
            if (rt.has(rtRel) && Relations::transitiveOver(ltRel, rtRel)) {
                if (Relations::isStrict(ltRel) || Relations::isStrict(rtRel))
                    result.set(Relations::getStrict(ltRel));
                else
                    result.set(ltRel);
            }
        }
    }
    return result.addImplied();
}

bool Relations::transitiveOver(Type fst, Type snd) {
    switch (fst) {
    case SLE:
    case SLT:
        return snd == SLE || snd == SLT;
    case ULE:
    case ULT:
        return snd == ULE || snd == ULT;
    case SGE:
    case SGT:
        return snd == SGE || snd == SGT;
    case UGE:
    case UGT:
        return snd == UGE || snd == UGT;
    case EQ:
    case NE:
    case PT:
    case PF:
        return false;
    }
    assert(0 && "unreachable");
    abort();
}

bool Relations::isStrict(Type type) { return strict.has(type); }

bool Relations::isNonStrict(Type type) { return nonStrict.has(type); }

Relations::Type Relations::getStrict(Type type) {
    switch (type) {
    case Relations::SLT:
    case Relations::SLE:
        return Relations::SLT;
    case Relations::ULT:
    case Relations::ULE:
        return Relations::ULT;
    case Relations::SGT:
    case Relations::SGE:
        return Relations::SGT;
    case Relations::UGT:
    case Relations::UGE:
        return Relations::UGT;
    default:
        assert(0 && "no strict variant");
        abort();
    }
}

Relations::Type Relations::getNonStrict(Type type) {
    switch (type) {
    case Relations::SLT:
        return Relations::SLE;
    case Relations::ULT:
        return Relations::ULE;
    case Relations::SGT:
        return Relations::SGE;
    case Relations::UGT:
        return Relations::UGE;
    default:
        assert(0 && "no nonstrict variant");
        abort();
    }
}

#ifndef NDEBUG
std::ostream &operator<<(std::ostream &out, Relations::Type r) {
    switch (r) {
    case Relations::EQ:
        out << "EQ";
        break;
    case Relations::NE:
        out << "NE";
        break;
    case Relations::SLE:
        out << "SLE";
        break;
    case Relations::SLT:
        out << "SLT";
        break;
    case Relations::ULE:
        out << "ULE";
        break;
    case Relations::ULT:
        out << "ULT";
        break;
    case Relations::SGE:
        out << "SGE";
        break;
    case Relations::SGT:
        out << "SGT";
        break;
    case Relations::UGE:
        out << "UGE";
        break;
    case Relations::UGT:
        out << "UGT";
        break;
    case Relations::PT:
        out << "PT";
        break;
    case Relations::PF:
        out << "PF";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Relations &rels) {
    out << "[ ";
    for (Relations::Type type : Relations::all) {
        if (rels.has(type))
            out << type << " ";
    }
    out << "]";
    return out;
}
#endif

} // namespace vr
} // namespace dg
