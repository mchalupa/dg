#include "VR.h"

namespace dg {
namespace vr {

// *************************** iterators ****************************** //
bool VR::are(Handle lt, Relations::Type rel, Handle rt) const {
    return graph.areRelated(lt, rel, rt);
}

bool VR::are(Handle lt, Relations::Type rel, V rt) const {
    HandlePtr mRt = maybeGet(rt);

    if (mRt)
        return are(lt, rel, *mRt);

    return are(lt, rel, llvm::dyn_cast<BareC>(rt));
}

bool VR::are(Handle lt, Relations::Type rel, C cRt) const {
    // right cannot be expressed as a constant
    if (!cRt)
        return false;

    C boundLt;
    Relations relsLt;
    std::tie(boundLt, relsLt) =
            getBound(lt, Relations::isStrict(rel)
                                 ? Relations().set(Relations::getNonStrict(rel))
                                 : Relations().eq());

    // left is not bound in direction of the relation by a constant
    if (!boundLt)
        return false;

    // constant already is strictly related to the left value
    if (Relations::isStrict(rel) && relsLt.has(rel))
        return compare(boundLt, Relations::getNonStrict(rel), cRt);

    // otherwise just compare the constants
    return compare(boundLt, rel, cRt);
}

bool VR::are(V lt, Relations::Type rel, Handle rt) const {
    return are(rt, Relations::inverted(rel), lt);
}

bool VR::are(C lt, Relations::Type rel, Handle rt) const {
    return are(rt, Relations::inverted(rel), lt);
}

bool VR::are(V lt, Relations::Type rel, V rt) const {
    if (HandlePtr mLt = maybeGet(lt))
        return are(*mLt, rel, rt);

    if (HandlePtr mRt = maybeGet(lt))
        return are(llvm::dyn_cast<BareC>(lt), rel, *mRt);

    C cLt = llvm::dyn_cast<BareC>(lt);
    C cRt = llvm::dyn_cast<BareC>(rt);

    return cLt && cRt && compare(cLt, rel, cRt);
}

// *************************** iterators ****************************** //
VR::rel_iterator VR::begin_related(V val, const Relations &rels) const {
    assert(valToBucket.find(val) != valToBucket.end());
    Handle h = valToBucket.find(val)->second;
    return rel_iterator(*this, h, rels);
}

VR::rel_iterator VR::end_related(V /*val*/) const {
    return rel_iterator(*this);
}

VR::RelGraph::iterator VR::begin_related(Handle h,
                                         const Relations &rels) const {
    return graph.begin_related(h, rels);
}

VR::RelGraph::iterator VR::end_related(Handle h) const {
    return graph.end_related(h);
}

VR::rel_iterator VR::begin_all(V val) const {
    return begin_related(val, Relations().eq().lt().le().gt().ge());
}

VR::rel_iterator VR::end_all(V val) const { return end_related(val); }

VR::rel_iterator VR::begin_lesserEqual(V val) const {
    return begin_related(val, Relations().eq().lt().le());
}

VR::rel_iterator VR::end_lesserEqual(V val) const { return end_related(val); }

VR::plain_iterator VR::begin() const {
    return plain_iterator(bucketToVals.begin(), bucketToVals.end());
}

VR::plain_iterator VR::end() const {
    return plain_iterator(bucketToVals.end());
}

VR::RelGraph::iterator VR::begin_buckets(const Relations &rels) const {
    return graph.begin(rels);
}

VR::RelGraph::iterator VR::end_buckets() const { return graph.end(); }

// ****************************** get ********************************* //
VR::HandlePtr VR::maybeGet(V val) const {
    auto found = valToBucket.find(val);
    return (found == valToBucket.end() ? nullptr : &found->second.get());
}

VR::Handle VR::get(V val) {
    if (HandlePtr mh = maybeGet(val))
        return *mh;
    Handle newH = graph.getNewBucket();
    return add(val, newH);
}

VR::V VR::getAny(Handle h) const {
    auto found = bucketToVals.find(h);
    assert(found != bucketToVals.end() && !found->second.empty());
    return *found->second.begin();
}

VR::C VR::getAnyConst(Handle h) const {
    for (V val : bucketToVals.find(h)->second) {
        if (C c = llvm::dyn_cast<BareC>(val))
            return c;
    }
    return nullptr;
}

std::vector<VR::V> VR::getEqual(Handle h) const {
    std::set<V> resultSet = bucketToVals.find(h)->second;
    return {resultSet.begin(), resultSet.end()};
}

std::vector<VR::V> VR::getEqual(V val) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {val};
    return getEqual(*mH);
}

std::vector<VR::V> VR::getAllRelated(V val) const {
    std::vector<VR::V> result;
    std::transform(begin_related(val), end_related(val),
                   std::back_inserter(result),
                   [](const auto &pair) { return pair.first; });
    return result;
}

std::vector<VR::V> VR::getAllValues() const {
    return std::vector<VR::V>(begin(), end());
}

std::vector<VR::V> VR::getDirectlyRelated(V val, const Relations &rels) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {};
    RelationsMap related = graph.getRelated(*mH, rels, true);

    std::vector<VR::V> result;
    std::transform(
            related.begin(), related.end(), std::back_inserter(result),
            [this](const auto &pair) { return this->getAny(pair.first); });
    return result;
}

std::vector<VR::V> VR::getDirectlyLesser(V val) const {
    return getDirectlyRelated(val, Relations().lt());
}

std::vector<VR::V> VR::getDirectlyGreater(V val) const {
    return getDirectlyRelated(val, Relations().gt());
}

std::pair<VR::C, Relations> VR::getBound(Handle h, Relations rels) const {
    RelationsMap related = graph.getRelated(h, rels);

    C resultC = nullptr;
    Relations resultR;
    for (const auto &pair : related) {
        C c = getAnyConst(pair.first);
        if (c && (!resultC || compare(c, rels, resultC))) {
            resultC = c;
            resultR = pair.second;
        }
    }

    return {resultC, resultR};
}

std::pair<VR::C, Relations> VR::getBound(V val, Relations rel) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {llvm::dyn_cast<BareC>(val), Relations().eq()};

    return getBound(*mH, rel);
}

std::pair<VR::C, Relations> VR::getLowerBound(V val) const {
    return getBound(val, Relations().le());
}

std::pair<VR::C, Relations> VR::getUpperBound(V val) const {
    return getBound(val, Relations().ge());
}

VR::C VR::getLesserEqualBound(V val) const { return getLowerBound(val).first; }

VR::C VR::getGreaterEqualBound(V val) const { return getUpperBound(val).first; }

VR::HandlePtr VR::getHandleByPtr(Handle h) const {
    if (!h.hasRelation(Relations::PT))
        return nullptr;
    return &h.getRelated(Relations::PT);
}

std::vector<VR::V> VR::getValsByPtr(V from) const {
    HandlePtr mH = maybeGet(from);
    if (!mH)
        return {};
    HandlePtr toH = getHandleByPtr(*mH);
    if (!toH)
        return {};
    return getEqual(*toH);
}

std::set<std::pair<std::vector<VR::V>, std::vector<VR::V>>>
VR::getAllLoads() const {
    std::set<std::pair<std::vector<V>, std::vector<V>>> result;
    for (auto it = begin_buckets(Relations().pt()); it != end_buckets(); ++it) {
        std::set<V> fromValsSet = bucketToVals.find(it->from())->second;
        std::vector<V> fromVals(fromValsSet.begin(), fromValsSet.end());

        std::set<V> toValsSet = bucketToVals.find(it->from())->second;
        std::vector<V> toVals(toValsSet.begin(), toValsSet.end());

        result.emplace(std::move(fromVals), std::move(toVals));
    }
    return result;
}

// ************************** placeholder ***************************** //
VR::Handle VR::newPlaceholderBucket() { return graph.getNewBucket(); }

void VR::erasePlaceholderBucket(Handle h) { graph.erase(h); }

// ***************************** other ******************************** //
bool VR::compare(C lt, Relations::Type rel, C rt) {
    switch (rel) {
    case Relations::EQ:
        return lt->getSExtValue() == rt->getSExtValue();
    case Relations::NE:
        return lt->getSExtValue() != rt->getSExtValue();
    case Relations::LE:
        return lt->getSExtValue() <= rt->getSExtValue();
    case Relations::LT:
        return lt->getSExtValue() < rt->getSExtValue();
    case Relations::GE:
    case Relations::GT:
        return compare(rt, Relations::inverted(rel), lt);
    case Relations::PT:
    case Relations::PF:
        break;
    }
    assert(0 && "unreachable");
    abort();
}

bool VR::compare(C lt, Relations rels, C rt) {
    for (Relations::Type rel : Relations::all) {
        if (rels.has(rel) && compare(lt, rel, rt))
            return true;
    }
    return false;
}

bool VR::holdsAnyRelations() const {
    return !valToBucket.empty() && !graph.empty();
}

VR::Handle VR::getCorresponding(const VR &other, Handle otherH,
                                const std::vector<V> &otherEqual) {
    for (V val : otherEqual) {
        if (HandlePtr mH = maybeGet(val))
            return *mH;
    }

    if (!otherEqual.empty()) {
        Handle newH = graph.getNewBucket();
        return add(otherEqual[0], newH);
    }

    // otherwise this is a placeholder bucket, therefore it is pointed from
    // other bucket
    assert(otherH.hasRelation(Relations::PF));
    Handle otherFromH = otherH.getRelated(Relations::PF);
    Handle thisFromH = getCorresponding(other, otherFromH);

    if (thisFromH.hasRelation(Relations::PT))
        return thisFromH.getRelated(Relations::PT);
    updateChanged(true);
    return graph.getNewBucket();
}

VR::Handle VR::getCorresponding(const VR &other, Handle otherH) {
    return getCorresponding(other, otherH, other.getEqual(otherH));
}

VR::Handle VR::getAndMerge(const VR &other, Handle otherH) {
    std::vector<V> otherEqual = other.getEqual(otherH);
    Handle thisH = getCorresponding(other, otherH, otherEqual);

    for (V val : otherEqual)
        add(val, thisH);

    return thisH;
}

void VR::merge(const VR &other, Relations relations) {
    for (const auto &edge : other.graph) {
        if (!relations.has(edge.rel()))
            continue;

        Handle thisToH = getAndMerge(other, edge.to());
        Handle thisFromH = getCorresponding(other, edge.from());

        bool ch = graph.addRelation(thisFromH, edge.rel(), thisToH);
        updateChanged(ch);
    }
}

VR::Handle VR::add(V val, Handle h, std::set<V> &vals) {
    valToBucket.emplace(val, h);
    vals.emplace(val);
    updateChanged(true);
    return h;
}

VR::Handle VR::add(V val, Handle h) {
    add(val, h, bucketToVals[h]);

    C c = llvm::dyn_cast<BareC>(val);
    if (!c)
        return h;

    for (auto &pair : bucketToVals) {
        if (pair.second.empty())
            continue;

        Handle otherH = pair.first;
        if (C otherC = llvm::dyn_cast<BareC>(getAny(otherH))) {
            if (compare(c, Relations::EQ, otherC)) {
                graph.addRelation(h, Relations::EQ, otherH);
                assert(valToBucket.find(val) != valToBucket.end());
                return valToBucket.find(val)->second;
            }

            if (compare(c, Relations::LT, otherC))
                graph.addRelation(h, Relations::LT, otherH);

            else if (compare(c, Relations::GT, otherC))
                graph.addRelation(h, Relations::GT, otherH);
        }
    }

    return h;
}

void VR::areMerged(Handle to, Handle from) {
    std::set<V> toVals = bucketToVals.find(to)->second;
    std::set<V> fromVals = bucketToVals.find(from)->second;

    for (V val : fromVals)
        add(val, to, toVals);
}

} // namespace vr
} // namespace dg