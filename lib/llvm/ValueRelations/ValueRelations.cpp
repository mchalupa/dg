#include "dg/llvm/ValueRelations/ValueRelations.h"
#ifndef NDEBUG
#include <iostream>
#endif
namespace dg {
namespace vr {

// *************************** iterators ****************************** //
bool ValueRelations::_are(Handle lt, Relations::Type rel, Handle rt) const {
    return graph.areRelated(lt, rel, rt);
}

bool ValueRelations::_are(Handle lt, Relations::Type rel, V rt) const {
    HandlePtr mRt = maybeGet(rt);

    if (mRt)
        return are(lt, rel, *mRt);

    return are(lt, rel, llvm::dyn_cast<BareC>(rt));
}

bool ValueRelations::_are(Handle lt, Relations::Type rel, C cRt) const {
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

bool ValueRelations::_are(V lt, Relations::Type rel, Handle rt) const {
    return are(rt, Relations::inverted(rel), lt);
}

bool ValueRelations::_are(C lt, Relations::Type rel, Handle rt) const {
    return are(rt, Relations::inverted(rel), lt);
}

bool ValueRelations::_are(V lt, Relations::Type rel, V rt) const {
    if (HandlePtr mLt = maybeGet(lt))
        return are(*mLt, rel, rt);

    if (HandlePtr mRt = maybeGet(lt))
        return are(llvm::dyn_cast<BareC>(lt), rel, *mRt);

    C cLt = llvm::dyn_cast<BareC>(lt);
    C cRt = llvm::dyn_cast<BareC>(rt);

    return cLt && cRt && compare(cLt, rel, cRt);
}

// *************************** iterators ****************************** //
ValueRelations::rel_iterator
ValueRelations::begin_related(V val, const Relations &rels, bool invert) const {
    assert(valToBucket.find(val) != valToBucket.end());
    Handle h = valToBucket.find(val)->second;
    return rel_iterator(*this, h, rels, invert);
}

ValueRelations::rel_iterator ValueRelations::end_related(V /*val*/) const {
    return rel_iterator(*this);
}

ValueRelations::RelGraph::iterator
ValueRelations::begin_related(Handle h, const Relations &rels) const {
    return graph.begin_related(h, rels);
}

ValueRelations::RelGraph::iterator ValueRelations::end_related(Handle h) const {
    return graph.end_related(h);
}

ValueRelations::rel_iterator ValueRelations::begin_all(V val) const {
    return begin_related(val, Relations().eq().lt().le().gt().ge(), true);
}

ValueRelations::rel_iterator ValueRelations::end_all(V val) const {
    return end_related(val);
}

ValueRelations::rel_iterator ValueRelations::begin_lesserEqual(V val) const {
    return begin_related(val, Relations().eq().gt().ge(), true);
}

ValueRelations::rel_iterator ValueRelations::end_lesserEqual(V val) const {
    return end_related(val);
}

ValueRelations::plain_iterator ValueRelations::begin() const {
    return plain_iterator(bucketToVals.begin(), bucketToVals.end());
}

ValueRelations::plain_iterator ValueRelations::end() const {
    return plain_iterator(bucketToVals.end());
}

ValueRelations::RelGraph::iterator
ValueRelations::begin_buckets(const Relations &rels) const {
    return graph.begin(rels);
}

ValueRelations::RelGraph::iterator ValueRelations::end_buckets() const {
    return graph.end();
}

// ****************************** get ********************************* //
ValueRelations::HandlePtr ValueRelations::maybeGet(V val) const {
    auto found = valToBucket.find(val);
    return (found == valToBucket.end() ? nullptr : &found->second.get());
}

std::pair<ValueRelations::BRef, bool> ValueRelations::get(V val) {
    if (HandlePtr mh = maybeGet(val))
        return {*mh, false};
    Handle newH = graph.getNewBucket();
    return add(val, newH);
}

ValueRelations::V ValueRelations::getAny(Handle h) const {
    auto found = bucketToVals.find(h);
    assert(found != bucketToVals.end() && !found->second.empty());
    return *found->second.begin();
}

ValueRelations::C ValueRelations::getAnyConst(Handle h) const {
    for (V val : bucketToVals.find(h)->second) {
        if (C c = llvm::dyn_cast<BareC>(val))
            return c;
    }
    return nullptr;
}

std::vector<ValueRelations::V> ValueRelations::getEqual(Handle h) const {
    std::set<V> resultSet = bucketToVals.find(h)->second;
    return {resultSet.begin(), resultSet.end()};
}

std::vector<ValueRelations::V> ValueRelations::getEqual(V val) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {val};
    return getEqual(*mH);
}

std::vector<ValueRelations::V> ValueRelations::getAllRelated(V val) const {
    std::vector<ValueRelations::V> result;
    std::transform(begin_related(val, allRelations), end_related(val),
                   std::back_inserter(result),
                   [](const auto &pair) { return pair.first; });
    return result;
}

std::vector<ValueRelations::V> ValueRelations::getAllValues() const {
    return std::vector<ValueRelations::V>(begin(), end());
}

std::vector<ValueRelations::V>
ValueRelations::getDirectlyRelated(V val, const Relations &rels) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {};
    RelationsMap related = graph.getRelated(*mH, rels, true);

    std::vector<ValueRelations::V> result;
    std::transform(
            related.begin(), related.end(), std::back_inserter(result),
            [this](const auto &pair) { return this->getAny(pair.first); });
    return result;
}

std::vector<ValueRelations::V> ValueRelations::getDirectlyLesser(V val) const {
    return getDirectlyRelated(val, Relations().gt());
}

std::vector<ValueRelations::V> ValueRelations::getDirectlyGreater(V val) const {
    return getDirectlyRelated(val, Relations().lt());
}

std::pair<ValueRelations::C, Relations>
ValueRelations::getBound(Handle h, Relations rels) const {
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

std::pair<ValueRelations::C, Relations>
ValueRelations::getBound(V val, Relations rel) const {
    HandlePtr mH = maybeGet(val);
    if (!mH)
        return {llvm::dyn_cast<BareC>(val), Relations().eq()};

    return getBound(*mH, rel);
}

std::pair<ValueRelations::C, Relations>
ValueRelations::getLowerBound(V val) const {
    return getBound(val, Relations().ge());
}

std::pair<ValueRelations::C, Relations>
ValueRelations::getUpperBound(V val) const {
    return getBound(val, Relations().le());
}

ValueRelations::C ValueRelations::getLesserEqualBound(V val) const {
    return getLowerBound(val).first;
}

ValueRelations::C ValueRelations::getGreaterEqualBound(V val) const {
    return getUpperBound(val).first;
}

ValueRelations::HandlePtr ValueRelations::getHandleByPtr(Handle h) const {
    if (!h.hasRelation(Relations::PT))
        return nullptr;
    return &h.getRelated(Relations::PT);
}

const std::vector<ValueRelations::V>
ValueRelations::getValsByPtr(V from) const {
    HandlePtr mH = maybeGet(from);
    if (!mH)
        return {};
    HandlePtr toH = getHandleByPtr(*mH);
    if (!toH)
        return {};
    return getEqual(*toH);
}

std::set<std::pair<std::vector<ValueRelations::V>,
                   std::vector<ValueRelations::V>>>
ValueRelations::getAllLoads() const {
    std::set<std::pair<std::vector<V>, std::vector<V>>> result;
    for (auto it = begin_buckets(Relations().pt()); it != end_buckets(); ++it) {
        std::set<V> fromValsSet = bucketToVals.find(it->from())->second;
        std::vector<V> fromVals(fromValsSet.begin(), fromValsSet.end());

        std::set<V> toValsSet = bucketToVals.find(it->to())->second;
        std::vector<V> toVals(toValsSet.begin(), toValsSet.end());

        result.emplace(std::move(fromVals), std::move(toVals));
    }
    return result;
}

// ************************** placeholder ***************************** //
void ValueRelations::erasePlaceholderBucket(Handle h) {
    auto found = bucketToVals.find(h);
    assert(found != bucketToVals.end());
    for (V val : found->second) {
        assert(valToBucket.find(val) != valToBucket.end() &&
               valToBucket.at(val) == h);
        valToBucket.erase(val);
    }
    bucketToVals.erase(h);
    graph.erase(h);
}

// ***************************** other ******************************** //
bool ValueRelations::compare(C lt, Relations::Type rel, C rt) {
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

bool ValueRelations::compare(C lt, Relations rels, C rt) {
    for (Relations::Type rel : Relations::all) {
        if (rels.has(rel) && compare(lt, rel, rt))
            return true;
    }
    return false;
}

bool ValueRelations::holdsAnyRelations() const {
    return !valToBucket.empty() && !graph.empty();
}

ValueRelations::Handle
ValueRelations::getCorresponding(const ValueRelations &other, Handle otherH,
                                 const std::vector<V> &otherEqual) {
    if (otherEqual.empty()) { // other is a placeholder bucket, therefore it is
                              // pointed to from other bucket
        assert(otherH.hasRelation(Relations::PF));
        Handle otherFromH = otherH.getRelated(Relations::PF);
        Handle thisFromH = getCorresponding(other, otherFromH);

        Handle h = newPlaceholderBucket(thisFromH);
        bool ch = graph.addRelation(thisFromH, Relations::PT, h);
        updateChanged(ch);
        return h;
    }

    // otherwise find unique handle for all equal elements from other
    HandlePtr mH = nullptr;
    for (V val : otherEqual) {
        HandlePtr oH = maybeGet(val);
        if (!mH) // first handle found
            mH = oH;
        else if (oH && oH != mH) { // found non-equal handle in this
            set(*oH, Relations::EQ, *mH);
            mH = maybeGet(val); // update possibly invalidated handle
            assert(mH);
        }
    }
    return mH ? *mH : add(otherEqual[0], graph.getNewBucket()).first.get();
}

ValueRelations::Handle
ValueRelations::getCorresponding(const ValueRelations &other, Handle otherH) {
    return getCorresponding(other, otherH, other.getEqual(otherH));
}

ValueRelations::Handle ValueRelations::getAndMerge(const ValueRelations &other,
                                                   Handle otherH) {
    const std::vector<V> &otherEqual = other.getEqual(otherH);
    Handle thisH = getCorresponding(other, otherH, otherEqual);

    for (V val : otherEqual)
        add(val, thisH);

    return thisH;
}

bool ValueRelations::merge(const ValueRelations &other, Relations relations) {
    bool noConflict = true;
    for (const auto &edge : other.graph) {
        if (!relations.has(edge.rel()) ||
            (edge.rel() == Relations::EQ && !other.hasEqual(edge.to())))
            continue;

        Handle thisToH = getAndMerge(other, edge.to());
        Handle thisFromH = getCorresponding(other, edge.from());

        if (!graph.haveConflictingRelation(thisFromH, edge.rel(), thisToH)) {
            bool ch = graph.addRelation(thisFromH, edge.rel(), thisToH);
            updateChanged(ch);
        } else
            noConflict = false;
    }
    return noConflict;
}

void ValueRelations::add(V val, Handle h, std::set<V> &vals) {
    ValToBucket::iterator it = valToBucket.lower_bound(val);
    // val already bound to a handle
    if (it != valToBucket.end() && !(valToBucket.key_comp()(val, it->first))) {
        // it is already bound to passed handle
        if (it->second == h)
            return;
        V oldVal = it->first;
        Handle oldH = it->second;
        assert(bucketToVals.find(oldH) != bucketToVals.end());
        assert(bucketToVals.at(oldH).find(oldVal) !=
               bucketToVals.at(oldH).end());
        bucketToVals.find(oldH)->second.erase(oldVal);
        it->second = h;
    } else
        valToBucket.emplace_hint(it, val, h);

    assert(valToBucket.find(val)->second == h);
    vals.emplace(val);
    updateChanged(true);
}

std::pair<ValueRelations::BRef, bool> ValueRelations::add(V val, Handle h) {
    add(val, h, bucketToVals[h]);

    C c = llvm::dyn_cast<BareC>(val);
    if (!c)
        return {h, false};

    for (auto &pair : bucketToVals) {
        if (pair.second.empty())
            continue;

        Handle otherH = pair.first;
        if (C otherC = getAnyConst(otherH)) {
            if (compare(c, Relations::EQ, otherC)) {
                graph.addRelation(h, Relations::EQ, otherH);
                assert(valToBucket.find(val) != valToBucket.end());
                return {valToBucket.find(val)->second, true};
            }

            if (compare(c, Relations::LT, otherC))
                graph.addRelation(h, Relations::LT, otherH);

            else if (compare(c, Relations::GT, otherC))
                graph.addRelation(h, Relations::GT, otherH);
        }
    }

    return {h, false};
}

void ValueRelations::areMerged(Handle to, Handle from) {
    std::set<V> &toVals = bucketToVals.find(to)->second;
    assert(bucketToVals.find(from) != bucketToVals.end());
    const std::set<V> fromVals = bucketToVals.find(from)->second;

    for (V val : fromVals)
        add(val, to, toVals);

    assert(bucketToVals.at(from).empty());
    bucketToVals.erase(from);
}

std::string strip(std::string str, size_t skipSpaces) {
    assert(!str.empty() && !std::isspace(str[0]));

    size_t lastIndex = 0;
    for (size_t i = 0; i < skipSpaces; ++i) {
        size_t nextIndex = str.find(' ', lastIndex + 1);
        if (nextIndex == std::string::npos)
            return str;
        lastIndex = nextIndex;
    }
    return str.substr(0, lastIndex);
}

#ifndef NDEBUG
void dump(std::ostream &out, ValueRelations::Handle h,
          const ValueRelations::BucketToVals &map) {
    auto found = map.find(h);
    assert(found != map.end());
    const std::set<ValueRelations::V> &vals = found->second;

    out << "{{ ";
    if (vals.empty())
        out << "placeholder ";
    else
        for (ValueRelations::V val : vals)
            out << (val == *vals.begin() ? "" : " | ")
                << strip(debug::getValName(val), 4);
    out << " }}";
}

std::ostream &operator<<(std::ostream &out, const ValueRelations &vr) {
    for (const auto &edge : vr.graph) {
        if (edge.rel() == Relations::EQ) {
            if (!edge.to().hasAnyRelation()) {
                out << "              ";
                dump(out, edge.to(), vr.bucketToVals);
                out << "\n";
            }
            continue;
        }
        out << "    " << edge << "    ";
        dump(out, edge.from(), vr.bucketToVals);
        out << " " << edge.rel() << " ";
        dump(out, edge.to(), vr.bucketToVals);
        out << "\n";
    }
    return out;
}
#endif
} // namespace vr
} // namespace dg