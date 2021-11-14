#include "dg/ValueRelations/Relations.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace dg {
namespace vr {

const std::array<Relations::Type, Relations::total> Relations::all = {
        EQ, NE, LE, LT, GE, GT, PT, PF};

Relations::Type Relations::inverted(Relations::Type type) {
    switch (type) {
    case EQ:
        return EQ;
    case NE:
        return NE;
    case LE:
        return GE;
    case LT:
        return GT;
    case GE:
        return LE;
    case GT:
        return LT;
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
    case LE:
        return GT;
    case LT:
        return GE;
    case GE:
        return LT;
    case GT:
        return LE;
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
        return Relations().ne().lt().gt();
    case NE:
        return Relations().eq();
    case LT:
        return Relations().eq().gt().ge();
    case GT:
        return Relations().eq().lt().le();
    case LE:
        return Relations().gt();
    case GE:
        return Relations().lt();
    case PT:
    case PF:
        return Relations();
    }
    assert(0 && "unreachable");
    abort();
}

Relations::Type Relations::get() const {
    for (Type rel : {EQ, LT, GT, NE, LE, GE, PT, PF}) {
        if (has(rel))
            return rel;
    }
    assert(0 && "unreachable");
    abort();
}

Relations &Relations::addImplied() {
    if (has(EQ))
        le().ge();
    if (has(LT))
        le().ne();
    if (has(GT))
        ge().ne();
    return *this;
}

Relations &Relations::invert() {
    std::bitset<8> newBits;
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
    for (Relations::Type type : {Relations::LT, Relations::GT}) {
        if (augmented.has(type)) {
            augmented.set(Relations::getNonStrict(type));
        } else if (augmented.has(Relations::getNonStrict(type))) {
            augmented.set(type);
            augmented.eq();
        }
    }
    return augmented;
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
                    result.set(ltRel);
                else
                    result.set(ltRel);
            }
        }
    }
    return result.addImplied();
}

bool Relations::transitiveOver(Type fst, Type snd) {
    switch (fst) {
    case LE:
    case LT:
        return snd == LE || snd == LT;
    case GE:
    case GT:
        return snd == GE || snd == GT;
    case EQ:
    case NE:
    case PT:
    case PF:
        return false;
    }
    assert(0 && "unreachable");
    abort();
}

bool Relations::isStrict(Type type) {
    return type == Relations::LT || type == Relations::GT;
}

bool Relations::isNonStrict(Type type) {
    return type == Relations::LE || type == Relations::GE;
}

Relations::Type Relations::getStrict(Type type) {
    switch (type) {
    case Relations::LE:
        return Relations::LT;
    case Relations::GE:
        return Relations::GT;
    default:
        assert(0 && "no strict variant");
        abort();
    }
}

Relations::Type Relations::getNonStrict(Type type) {
    switch (type) {
    case Relations::LT:
        return Relations::LE;
    case Relations::GT:
        return Relations::GE;
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
    case Relations::LE:
        out << "LE";
        break;
    case Relations::LT:
        out << "LT";
        break;
    case Relations::GE:
        out << "GE";
        break;
    case Relations::GT:
        out << "GT";
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
