#include "RelationsGraph.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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

using RelationsMap = RelationsGraph::RelationsMap;

bool strict(Relations::Type type) {
    return type == Relations::LT || type == Relations::GT;
}

Relations::Type getNonStrict(Relations::Type type) {
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

Relations getAugmented(const Relations &relations) {
    Relations augmented = relations;
    for (Relations::Type type : {Relations::LT, Relations::GT}) {
        if (augmented.has(type) || augmented.has(getNonStrict(type))) {
            augmented.set(type);
            augmented.set(getNonStrict(type));
        }
    }
    return augmented;
}

RelationsMap &filterResult(const Relations &relations, RelationsMap &result) {
    for (auto it = result.begin(); it != result.end();) {
        if (!it->second.anyCommon(relations))
            it = result.erase(it);
        else
            ++it;
    }
    return result;
}

bool processEdge(Bucket::RelationEdge &edge, Relations::Type strictRel,
                 Relations &updated, bool toFirstStrict,
                 const std::set<Bucket::RelationEdge> &firstStrictEdges) {
    if (!Relations::transitiveOver(strictRel, edge.rel())) // edge not relevant
        return true;
    if (!toFirstStrict) { // finding all strictly related
        updated.set(strictRel);
        return false;
    }
    // else unsetting all that are not really first strict

    bool targetOfFSE = updated.has(strictRel); // FSE = first strict edge

    if (!targetOfFSE) {
        updated.set(getNonStrict(strictRel), false);
        return false;
    }

    // else possible false strict
    bool thisIsFSE = firstStrictEdges.find(edge) != firstStrictEdges.end();

    if (thisIsFSE) { // strict relation set by false first strict
        updated.set(strictRel, false);
        updated.set(getNonStrict(strictRel), false);
    }
    return true; // skip because search will happen from target sooner or later
}

RelationsMap getAugmentedRelated(const RelationsGraph &graph,
                                 const Bucket &start,
                                 const Relations &relations,
                                 bool toFirstStrict) {
    RelationsMap result;
    result[start].set(Relations::EQ);

    std::set<Bucket::RelationEdge> firstStrictEdges;
    for (auto it = graph.begin_related(start, relations);
         it != graph.end_related(start);
         /*incremented in body ++it*/) {
        result[it->to()].set(it->rel());

        if (strict(it->rel())) {
            firstStrictEdges.emplace(*it);
            it.skipSuccessors();
        } else
            ++it;
    }

    Bucket::ConstBucketSet nestedVisited;
    for (Bucket::RelationEdge edge : firstStrictEdges) {
        const Bucket &nestedStart = edge.to();
        Relations::Type strictRel = edge.rel();

        bool shouldSkip = false;
        for (auto it = nestedStart.begin(nestedVisited, relations, true, true);
             it != nestedStart.end(nestedVisited);
             shouldSkip ? it.skipSuccessors() : ++it) {
            shouldSkip = processEdge(*it, strictRel, result[it->to()],
                                     toFirstStrict, firstStrictEdges);
            if (shouldSkip)
                nestedVisited.erase(it->to());
        }
    }
    return result;
}

RelationsMap RelationsGraph::getRelated(const Bucket &start,
                                        const Relations &relations,
                                        bool toFirstStrict) const {
    Relations augmented = getAugmented(relations);

    RelationsMap result =
            getAugmentedRelated(*this, start, augmented, toFirstStrict);

    for (auto &pair : result) {
        pair.second.addImplied();
    }

    return filterResult(relations, result);
}

Relations fromMaybeBetween(const RelationsGraph &graph, const Bucket &lt,
                           const Bucket &rt, Relations *maybeBetween) {
    return maybeBetween ? *maybeBetween : graph.relationsBetween(lt, rt);
}

bool RelationsGraph::areRelated(const Bucket &lt, Relations::Type type,
                                const Bucket &rt,
                                Relations *maybeBetween) const {
    Relations between = fromMaybeBetween(*this, lt, rt, maybeBetween);
    return between.has(type);
}

bool RelationsGraph::haveConflictingRelation(const Bucket &lt,
                                             Relations::Type type,
                                             const Bucket &rt,
                                             Relations *maybeBetween) const {
    switch (type) {
    case Relations::EQ:
    case Relations::NE:
    case Relations::LT:
    case Relations::LE:
    case Relations::GT:
    case Relations::GE: {
        Relations between = fromMaybeBetween(*this, lt, rt, maybeBetween);
        return between.conflictsWith(type);
    }
    case Relations::PT:
        return lt.hasRelation(type) &&
               haveConflictingRelation(lt.getRelated(type), Relations::EQ, rt);
    case Relations::PF:
        return haveConflictingRelation(rt, Relations::inverted(type), lt);
    }
    assert(0 && "unreachable");
    abort();
}

Bucket::BucketSet getIntersectingNonstrict(const RelationsGraph &graph,
                                           const Bucket &lt, const Bucket &rt) {
    RelationsMap ltGE = graph.getRelated(lt, Relations().ge());
    RelationsMap rtLE = graph.getRelated(rt, Relations().le());

    RelationsMap result;
    std::set_intersection(ltGE.begin(), ltGE.end(), rtLE.begin(), rtLE.end(),
                          std::inserter(result, result.begin()),
                          [](auto &ltPair, auto &rtPair) {
                              return ltPair.first < rtPair.first;
                          });

    Bucket::BucketSet other;
    for (auto &pair : result)
        other.emplace(const_cast<Bucket &>(pair.first.get()));

    return other;
}

void RelationsGraph::addRelation(const Bucket &lt, Relations::Type type,
                                 const Bucket &rt, Relations *maybeBetween) {
    Bucket &mLt = const_cast<Bucket &>(lt);
    Bucket &mRt = const_cast<Bucket &>(rt);

    Relations between = fromMaybeBetween(*this, lt, rt, maybeBetween);
    if (areRelated(lt, type, rt, &between))
        return;
    assert(!haveConflictingRelation(lt, type, rt, &between));

    switch (type) {
    case Relations::EQ:
        if (lt.hasRelation(Relations::PT) && rt.hasRelation(Relations::PT)) {
            addRelation(lt.getRelated(Relations::PT), Relations::EQ,
                        rt.getRelated(Relations::PT));
        }
        return setEqual(mLt, mRt);
    case Relations::NE: {
        for (Relations::Type rel : {Relations::LT, Relations::GT}) {
            if (areRelated(lt, getNonStrict(rel), rt, &between)) {
                unsetRelated(mLt, getNonStrict(rel), mRt);
                return addRelation(lt, rel, rt, &between);
            }
        }
        break; // jump after switch
    }
    case Relations::LT:
        if (areRelated(lt, Relations::LE, rt, &between))
            unsetRelated(mLt, Relations::LE, mRt);
        break; // jump after switch
    case Relations::LE:
        if (areRelated(lt, Relations::NE, rt, &between)) {
            unsetRelated(mLt, Relations::NE, mRt);
            return addRelation(lt, Relations::LT, rt, &between);
        }
        if (areRelated(lt, Relations::GE, rt, &between)) {
            Bucket::BucketSet intersect =
                    getIntersectingNonstrict(*this, lt, rt);
            Bucket &first = *intersect.begin();
            for (auto it = std::next(intersect.begin()); it != intersect.end();
                 ++it)
                setEqual(first, *it);
            return;
        }
    }
}
setRelated(mLt, type, mRt);
}

} // namespace vr
} // namespace dg
