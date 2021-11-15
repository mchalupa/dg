#ifndef DG_LLVM_VALUE_RELATIONS_RELATIONS_GRAPH_H_
#define DG_LLVM_VALUE_RELATIONS_RELATIONS_GRAPH_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stack>

#include "Bucket.h"

namespace dg {
namespace vr {

template <typename T>
class RelationsGraph {
  public:
    using RelationsMap =
            std::map<std::reference_wrapper<const Bucket>, Relations>;

    const Bucket *getBorderB(size_t id) const {
        assert(id != std::string::npos);
        for (const auto &pair : borderBuckets)
            if (pair.first == id)
                return &pair.second.get();
        return nullptr;
    }

    size_t getBorderId(const Bucket &h) const {
        for (const auto &pair : borderBuckets)
            if (pair.second == h)
                return pair.first;
        return std::string::npos;
    }

  private:
    using UniqueBucketSet = std::set<std::unique_ptr<Bucket>>;

    class EdgeIterator {
        using BucketIterator = UniqueBucketSet::iterator;
        Bucket::iterator::Visited visited;

        BucketIterator bucketIt;
        BucketIterator endIt;
        Bucket::iterator edgeIt;

        void nextViableEdge() {
            while (edgeIt == (*bucketIt)->end(visited)) {
                ++bucketIt;
                if (bucketIt == endIt)
                    return;
                edgeIt = (*bucketIt)->begin(visited, edgeIt.allowedEdges,
                                            edgeIt.undirectedOnly,
                                            edgeIt.relationsFocused);
            }
        }

      public:
        using value_type = Bucket::RelationEdge;
        using difference_type = int64_t;
        using iterator_category = std::output_iterator_tag;
        using reference = value_type &;
        using pointer = value_type *;

        EdgeIterator(BucketIterator end)
                : bucketIt(end), endIt(end), edgeIt(visited) {}
        EdgeIterator(BucketIterator start, BucketIterator end,
                     const Relations &a, bool u, bool r)
                : bucketIt(start), endIt(end),
                  edgeIt((*bucketIt)->begin(visited, a, u, r)) {
            assert(bucketIt != endIt && "at least one bucket");
            nextViableEdge();
        }

        EdgeIterator(const EdgeIterator &other)
                : visited(other.visited), bucketIt(other.bucketIt),
                  endIt(other.endIt), edgeIt(other.edgeIt) {
            edgeIt.setVisited(visited);
        }

        EdgeIterator(EdgeIterator &&other)
                : visited(std::move(other.visited)),
                  bucketIt(std::move(other.bucketIt)),
                  endIt(std::move(other.endIt)),
                  edgeIt(std::move(other.edgeIt)) {
            edgeIt.setVisited(visited);
        }

        EdgeIterator &operator=(EdgeIterator other) {
            swap(*this, other);
            return *this;
        }

        friend void swap(EdgeIterator &lt, EdgeIterator &rt) {
            using std::swap;

            swap(lt.visited, rt.visited);
            swap(lt.bucketIt, rt.bucketIt);
            swap(lt.endIt, rt.endIt);
            swap(lt.edgeIt, rt.edgeIt);
        }

        friend bool operator==(const EdgeIterator &lt, const EdgeIterator &rt) {
            return lt.bucketIt == rt.bucketIt && lt.edgeIt == rt.edgeIt;
        }

        friend bool operator!=(const EdgeIterator &lt, const EdgeIterator &rt) {
            return !(lt == rt);
        }

        EdgeIterator &operator++() {
            ++edgeIt;
            nextViableEdge();
            return *this;
        }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        void skipSuccessors() {
            edgeIt.skipSuccessors();
            nextViableEdge();
        }

        const Bucket::RelationEdge &operator*() const { return *edgeIt; }
        const Bucket::RelationEdge *operator->() const { return &(*edgeIt); }
    };

    //*********************** end iterator stuff **********************

    T &reported;
    UniqueBucketSet buckets;
    size_t lastId = 0;

    std::vector<std::pair<size_t, std::reference_wrapper<const Bucket>>>
            borderBuckets;

    bool setEqual(Bucket &to, Bucket &from) {
        assert(to != from);
        if (getBorderId(from) != std::string::npos) {
            assert(getBorderId(to) ==
                   std::string::npos); // cannot merge two border buckets
            for (auto &pair : borderBuckets) {
                if (from == pair.second)
                    pair.second = to;
            }
        }
        reported.areMerged(to, from);
        to.merge(from);
        erase(from);
        return true;
    }

    UniqueBucketSet::iterator getItFor(const Bucket &bucket) const {
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            if (**it == bucket)
                return it;
        }
        assert(0 && "unreachable");
        abort();
    }

    static RelationsMap &filterResult(const Relations &relations,
                                      RelationsMap &result) {
        for (auto it = result.begin(); it != result.end();) {
            if (!it->second.anyCommon(relations))
                it = result.erase(it);
            else
                ++it;
        }
        return result;
    }

    static bool processEdge(const Bucket::RelationEdge &edge,
                            Relations::Type strictRel, Relations &updated,
                            bool toFirstStrict,
                            const Bucket::iterator::Visited &firstStrictEdges) {
        if (!Relations::transitiveOver(strictRel,
                                       edge.rel())) // edge not relevant
            return true;
        if (!toFirstStrict) { // finding all strictly related
            updated.set(strictRel);
            return false;
        }
        // else unsetting all that are not really first strict

        bool targetOfFSE = updated.has(strictRel); // FSE = first strict edge

        if (!targetOfFSE) {
            updated.set(Relations::getNonStrict(strictRel), false);
            return false;
        }

        // else possible false strict
        bool thisIsFSE = firstStrictEdges.find(edge) != firstStrictEdges.end();

        if (thisIsFSE) { // strict relation set by false first strict
            updated.set(strictRel, false);
            updated.set(Relations::getNonStrict(strictRel), false);
        }
        return true; // skip because search will happen from target sooner or
                     // later
    }

    RelationsMap getAugmentedRelated(const Bucket &start,
                                     const Relations &relations,
                                     bool toFirstStrict) const {
        RelationsMap result;

        Bucket::iterator::Visited firstStrictEdges;
        for (auto it = begin_related(start, relations);
             it != end_related(start);
             /*incremented in body */) {
            result[it->to()].set(it->rel());

            if (Relations::isStrict(it->rel())) {
                firstStrictEdges.emplace(*it);
                it.skipSuccessors();
            } else
                ++it;
        }

        Bucket::iterator::Visited nestedVisited;
        for (Bucket::RelationEdge edge : firstStrictEdges) {
            const Bucket &nestedStart = edge.to();
            Relations::Type strictRel = edge.rel();

            bool shouldSkip = false;
            for (auto it = nestedStart.begin(nestedVisited, relations, true,
                                             true);
                 it != nestedStart.end(nestedVisited);
                 shouldSkip ? it.skipSuccessors() : ++it) {
                shouldSkip = processEdge(*it, strictRel, result[it->to()],
                                         toFirstStrict, firstStrictEdges);
                if (shouldSkip)
                    nestedVisited.erase(*it);
            }
        }
        return result;
    }

    Relations fromMaybeBetween(const Bucket &lt, const Bucket &rt,
                               Relations *maybeBetween) const {
        return maybeBetween ? *maybeBetween : relationsBetween(lt, rt);
    }

    Bucket::BucketSet getIntersectingNonstrict(const Bucket &lt,
                                               const Bucket &rt,
                                               Relations::Type type) {
        assert(type == Relations::SGE || type == Relations::UGE);
        assert(areRelated(lt, type, rt));
        RelationsMap ltGE = getRelated(lt, Relations().set(type));
        RelationsMap rtLE =
                getRelated(rt, Relations().set(Relations::inverted(type)));

        RelationsMap result;
        std::set_intersection(ltGE.begin(), ltGE.end(), rtLE.begin(),
                              rtLE.end(), std::inserter(result, result.begin()),
                              [](RelationsMap::value_type &ltPair,
                                 RelationsMap::value_type &rtPair) {
                                  return ltPair.first < rtPair.first;
                              });

        Bucket::BucketSet other;
        for (auto &pair : result) {
            Bucket &b = const_cast<Bucket &>(pair.first.get());
            assert(!other.contains(b));
            other.sure_emplace(b);
        }

        return other;
    }

  public:
    RelationsGraph(T &r) : reported(r) {}
    RelationsGraph(const RelationsGraph &) = delete;

    using iterator = EdgeIterator;

    Relations relationsBetween(const Bucket &lt, const Bucket &rt) const {
        RelationsMap result = getRelated(lt, allRelations);
        return result[rt];
    }

    iterator begin_related(const Bucket &start,
                           const Relations &relations) const {
        auto startIt = getItFor(start);
        auto endIt = std::next(startIt);
        return iterator(startIt, endIt, relations, true, true);
    }

    iterator begin_related(const Bucket &start) const {
        return begin_related(start, allRelations);
    }

    iterator end_related(const Bucket &start) const {
        auto endIt = ++getItFor(start);
        return iterator(endIt);
    }

    iterator begin() const {
        return begin(Relations().eq().ne().sle().slt().ule().ult().pt());
    }

    iterator begin(const Relations &relations,
                   bool undirectedOnly = true) const {
        if (!buckets.empty())
            return iterator(buckets.begin(), buckets.end(), relations,
                            undirectedOnly, false);
        return end();
    }

    iterator end() const { return iterator(buckets.end()); }

    RelationsMap getRelated(const Bucket &start, const Relations &relations,
                            bool toFirstStrict = false) const {
        Relations augmented = Relations::getAugmented(relations);

        RelationsMap result =
                getAugmentedRelated(start, augmented, toFirstStrict);

        for (auto &pair : result) {
            pair.second.addImplied();
        }

        return filterResult(relations, result);
    }

    bool areRelated(const Bucket &lt, Relations::Type type, const Bucket &rt,
                    Relations *maybeBetween = nullptr) const {
        Relations between = fromMaybeBetween(lt, rt, maybeBetween);
        return between.has(type);
    }

    bool haveConflictingRelation(const Bucket &lt, Relations::Type type,
                                 const Bucket &rt,
                                 Relations *maybeBetween = nullptr) const {
        switch (type) {
        case Relations::EQ:
        case Relations::NE:
        case Relations::SLT:
        case Relations::SLE:
        case Relations::ULT:
        case Relations::ULE:
        case Relations::SGT:
        case Relations::SGE:
        case Relations::UGT:
        case Relations::UGE: {
            Relations between = fromMaybeBetween(lt, rt, maybeBetween);
            return between.conflictsWith(type);
        }
        case Relations::PT:
            return lt.hasRelation(type) &&
                   haveConflictingRelation(lt.getRelated(type), Relations::EQ,
                                           rt);
        case Relations::PF:
            return haveConflictingRelation(rt, Relations::inverted(type), lt);
        }
        assert(0 && "unreachable");
        abort();
    }

    bool addRelation(const Bucket &lt, Relations::Type type, const Bucket &rt,
                     Relations *maybeBetween = nullptr) {
        Bucket &mLt = const_cast<Bucket &>(lt);
        Bucket &mRt = const_cast<Bucket &>(rt);

        Relations between = fromMaybeBetween(lt, rt, maybeBetween);
        if (areRelated(lt, type, rt, &between))
            return false;
        assert(!haveConflictingRelation(lt, type, rt, &between));

        switch (type) {
        case Relations::EQ:
            if (lt.hasRelation(Relations::PT) &&
                rt.hasRelation(Relations::PT)) {
                addRelation(lt.getRelated(Relations::PT), Relations::EQ,
                            rt.getRelated(Relations::PT));
            }
            return setEqual(mLt, mRt);

        case Relations::NE:
            for (Relations::Type rel : {Relations::SLT, Relations::ULT,
                                        Relations::SGT, Relations::UGT}) {
                if (areRelated(lt, rel, rt))
                    return false;
                if (areRelated(lt, Relations::getNonStrict(rel), rt,
                               &between)) {
                    unsetRelated(mLt, Relations::getNonStrict(rel), mRt);
                    return addRelation(lt, rel, rt, &between);
                }
            }
            break; // jump after switch

        case Relations::SLT:
        case Relations::ULT:
            if (areRelated(lt, Relations::getNonStrict(type), rt, &between))
                unsetRelated(mLt, Relations::getNonStrict(type), mRt);
            if (areRelated(lt, Relations::NE, rt, &between))
                unsetRelated(mLt, Relations::NE, mRt);
            break; // jump after switch

        case Relations::SLE:
        case Relations::ULE:
            if (areRelated(lt, Relations::NE, rt, &between)) {
                unsetRelated(mLt, Relations::NE, mRt);
                return addRelation(lt, Relations::getStrict(type), rt,
                                   &between);
            }
            if (areRelated(lt, Relations::inverted(type), rt, &between)) {
                Bucket::BucketSet intersect = getIntersectingNonstrict(
                        lt, rt, Relations::inverted(type));
                assert(intersect.size() >= 2);
                Bucket &first = *intersect.begin();
                for (auto it = std::next(intersect.begin());
                     it != intersect.end(); ++it)
                    setEqual(first, *it);
                return true;
            }
            break; // jump after switch

        case Relations::PT:
            if (lt.hasRelation(type))
                return addRelation(lt.getRelated(type), Relations::EQ, rt);
            break; // jump after switch

        case Relations::SGT:
        case Relations::SGE:
        case Relations::UGT:
        case Relations::UGE:
        case Relations::PF: {
            between.invert();
            return addRelation(rt, Relations::inverted(type), lt, &between);
        }
        }
        setRelated(mLt, type, mRt);
        return true;
    }

    const Bucket &getNewBucket() {
        auto pair = buckets.emplace(new Bucket(++lastId));
        return **pair.first;
    }

    const UniqueBucketSet &getBuckets() const { return buckets; }

    bool unset(const Relations &rels) {
        bool changed = false;
        for (const auto &bucketPtr : buckets) {
            changed |= bucketPtr->unset(rels);
        }
        return changed;
    }

    bool unset(const Bucket &bucket, const Relations &rels) {
        return const_cast<Bucket &>(bucket).unset(rels);
    }

    void erase(const Bucket &bucket) {
        Bucket &nBucket = const_cast<Bucket &>(bucket);
        nBucket.disconnect();

        auto it = getItFor(nBucket);
        buckets.erase(it);
    }

    bool empty() const { return buckets.empty(); }

    size_t size() const { return buckets.size(); }

    const Bucket &getBorderBucket(size_t id) {
        const Bucket &bucket = getNewBucket();
        assert(getBorderB(id) == nullptr);
        borderBuckets.emplace_back(id, bucket);
        return bucket;
    }

    void makeBorderBucket(const Bucket &b, size_t id) {
        size_t currentId = getBorderId(b);
        if (currentId == id)
            return;

        assert(getBorderB(id) == nullptr);
        assert(getBorderId(b) == std::string::npos);
        borderBuckets.emplace_back(id, b);
    }

#ifndef NDEBUG
    void dumpBorderBuckets(std::ostream &out = std::cerr) const {
        out << "[ ";
        for (auto &pair : borderBuckets)
            out << "(id " << pair.first << ", b " << pair.second.get().id
                << "), ";
        out << "]\n";
    }

    friend std::ostream &operator<<(std::ostream &out,
                                    const RelationsGraph &graph) {
        for (const auto &item : graph.buckets)
            out << "    " << *item << "\n";
        return out;
    }
#endif
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATIONS_GRAPH_H_
