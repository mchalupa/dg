#ifndef DG_LLVM_VALUE_RELATIONS_VR_H_
#define DG_LLVM_VALUE_RELATIONS_VR_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>

#include <map>

#ifndef NDEBUG
#include "getValName.h"
#include <iostream>
#endif

#include "RelationsGraph.h"

namespace dg {
namespace vr {

struct VR {
    using RelGraph = RelationsGraph<VR>;
    using Handle = const Bucket &;
    using RelationsMap = RelGraph::RelationsMap;
    using V = const llvm::Value *;
    using C = const llvm::ConstantInt *;

  private:
    using BareC = llvm::ConstantInt;
    using HandlePtr = const Bucket *;
    using BRef = std::reference_wrapper<const Bucket>;

    template <typename T>
    friend class RelationsGraph;
    friend class ValueIterator;

    RelGraph graph;

    std::map<V, BRef> valToBucket;
    std::map<BRef, std::set<V>> bucketToVals;

    bool changed = false;

    // ****************************** get ********************************* //
    HandlePtr maybeGet(Handle h) const { return &h; }
    HandlePtr maybeGet(V val) const;

    Handle get(Handle h) const { return h; }
    Handle get(V val);

    V getAny(Handle h) const;
    VR::C getAnyConst(Handle h) const;

    std::vector<V> getDirectlyRelated(V val, const Relations &rels) const;
    std::pair<VR::C, Relations> getBound(Handle h, Relations rel) const;
    std::pair<VR::C, Relations> getBound(V val, Relations rel) const;

    HandlePtr getHandleByPtr(Handle h) const;

    // ************************** general set ***************************** //
    template <typename X, typename Y>
    void set(X lt, Relations::Type rel, Y rt) {
        bool ch = graph.addRelation(get(lt), rel, get(rt));
        updateChanged(ch);
    }

    // ************************* general unset **************************** //
    void unset(Relations rels) {
        bool ch = graph.unset(rels);
        updateChanged(ch);
    }
    void unset(Relations::Type rel) { unset(Relations().set(rel)); }
    template <typename X>
    void unset(X val, Relations rels) {
        bool ch = graph.unset(get(val), rels);
        updateChanged(ch);
    }
    template <typename X>
    void unset(X val, Relations::Type rel) {
        unset(val, Relations().set(rel));
    }

    // ************************** general are ***************************** //
    bool are(Handle lt, Relations::Type rel, Handle rt) const;
    bool are(Handle lt, Relations::Type rel, V rt) const;
    bool are(Handle lt, Relations::Type rel, C rt) const;
    bool are(V lt, Relations::Type rel, Handle rt) const;
    bool are(C lt, Relations::Type rel, Handle rt) const;
    bool are(V lt, Relations::Type rel, V rt) const;

    // ************************** general has ***************************** //
    template <typename X>
    bool has(X val, Relations::Type rel) const {
        HandlePtr mVal = maybeGet(val);
        return mVal && mVal->hasRelation(rel);
    }
    template <typename X>
    bool has(X val, Relations rels) const {
        HandlePtr mVal = maybeGet(val);
        return mVal && mVal->hasAnyRelation(rels);
    }

    // *************************** iterators ***************************** //
    class RelatedValueIterator {
        using RelatedIterator = RelationsMap::iterator;
        using ValIterator = typename std::set<V>::const_iterator;

        const VR &vr;

        RelationsMap related;
        RelatedIterator bucketIt;
        ValIterator valueIt;

        bool isEnd = false;

        const std::set<V> &getCurrentEqual() const {
            return vr.bucketToVals.find(bucketIt->first)->second;
        }

        void nextViableValue() {
            while (valueIt == getCurrentEqual().end()) {
                ++bucketIt;
                if (bucketIt == related.end()) {
                    isEnd = true;
                    return;
                }
                valueIt = getCurrentEqual().begin();
            }
        }

      public:
        using value_type = std::pair<V, Relations>;
        using difference_type = int64_t;
        using iterator_category = std::input_iterator_tag;

        using reference = value_type;
        using pointer = const value_type *;

        // for end iterator
        RelatedValueIterator(const VR &v) : vr(v), isEnd(true) {}
        // for begin iterator
        RelatedValueIterator(const VR &v, Handle start,
                             const Relations &allowedEdges)
                : vr(v), related(vr.graph.getRelated(start, allowedEdges)),
                  bucketIt(related.begin()),
                  valueIt(getCurrentEqual().begin()) {
            assert(allowedEdges.has(Relations::EQ) &&
                   "at least one related bucket");
            nextViableValue();
        }

        friend bool operator==(const RelatedValueIterator &lt,
                               const RelatedValueIterator &rt) {
            return (lt.isEnd && rt.isEnd) ||
                   (!lt.isEnd && !rt.isEnd && lt.bucketIt == rt.bucketIt &&
                    lt.valueIt == rt.valueIt);
        }
        friend bool operator!=(const RelatedValueIterator &lt,
                               const RelatedValueIterator &rt) {
            return !(lt == rt);
        }

        RelatedValueIterator &operator++() {
            ++valueIt;
            nextViableValue();
            return *this;
        }

        RelatedValueIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        reference operator*() const { return {*valueIt, bucketIt->second}; }
    };

    class PlainValueIterator {
        using BucketIterator = std::map<BRef, std::set<V>>::const_iterator;
        using ValIterator = std::set<V>::iterator;

        BucketIterator bucketIt;
        BucketIterator endIt;
        ValIterator valueIt;

        bool nextViableValue() {
            while (valueIt == bucketIt->second.end()) {
                if (++bucketIt == endIt)
                    return false;
                valueIt = bucketIt->second.begin();
            }
            return true;
        }

      public:
        using value_type = V;
        using difference_type = int64_t;
        using iterator_category = std::input_iterator_tag;

        using reference = value_type;
        using pointer = const value_type *;

        // for end iterator
        PlainValueIterator(BucketIterator e) : bucketIt(e) {}
        // for begin iterator
        PlainValueIterator(BucketIterator b, BucketIterator e)
                : bucketIt(b), endIt(e), valueIt(b->second.begin()) {
            nextViableValue();
        }

        friend bool operator==(const PlainValueIterator &lt,
                               const PlainValueIterator &rt) {
            return lt.bucketIt == rt.bucketIt;
        }
        friend bool operator!=(const PlainValueIterator &lt,
                               const PlainValueIterator &rt) {
            return !(lt == rt);
        }

        PlainValueIterator &operator++() {
            ++valueIt;
            nextViableValue();
            return *this;
        }

        PlainValueIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        reference operator*() const { return *valueIt; }
        pointer operator->() const { return &(*valueIt); }
    };

    // ***************************** other ******************************** //
    static bool compare(C lt, Relations::Type rel, C rt);
    static bool compare(C lt, Relations rels, C rt);
    Handle getCorresponding(const VR &other, Handle otherH,
                            const std::vector<V> &otherEqual);
    Handle getCorresponding(const VR &other, Handle otherH);
    Handle getAndMerge(const VR &other, Handle current);
    Handle add(V val, Handle h, std::set<V> &vals);
    Handle add(V val, Handle h);
    void areMerged(Handle to, Handle from);

  public:
    using rel_iterator = RelatedValueIterator;
    using plain_iterator = PlainValueIterator;

    // ****************************** set ********************************* //
    template <typename X, typename Y>
    void setEqual(X lt, Y rt) {
        set(lt, Relations::EQ, rt);
    }
    template <typename X, typename Y>
    void setNonEqual(X lt, Y rt) {
        set(lt, Relations::NE, rt);
    }
    template <typename X, typename Y>
    void setLesser(X lt, Y rt) {
        set(lt, Relations::LT, rt);
    }
    template <typename X, typename Y>
    void setLesserEqual(X lt, Y rt) {
        set(lt, Relations::LE, rt);
    }
    template <typename X, typename Y>
    void setLoad(X from, Y to) {
        set(from, Relations::PT, to);
    }

    // ***************************** unset ******************************** //
    template <typename X>
    void unsetAllLoadsByPtr(X from) {
        unset(from, Relations::PT);
    }
    void unsetAllLoads() { unset(Relations::PT); }
    template <typename X>
    void unsetComparativeRelations(X val) {
        unset(val, comparative);
    }

    // ******************************* is ********************************** //
    template <typename X, typename Y>
    bool isEqual(X lt, Y rt) const {
        return are(lt, Relations::EQ, rt);
    }
    template <typename X, typename Y>
    bool isNonEqual(X lt, Y rt) const {
        return are(lt, Relations::NE, rt);
    }
    template <typename X, typename Y>
    bool isLesser(X lt, Y rt) const {
        return are(lt, Relations::LT, rt);
    }
    template <typename X, typename Y>
    bool isLesserEqual(X lt, Y rt) const {
        return are(lt, Relations::LE, rt);
    }
    template <typename X, typename Y>
    bool isLoad(X from, Y to) const {
        return are(from, Relations::PT, to);
    }

    // ****************************** has ********************************* //
    template <typename X>
    bool hasLoad(X from) const {
        return has(from, Relations::PT);
    }
    template <typename X, typename Y>
    bool hasConflictingRelations(X lt, Relations::Type rel, Y rt) const {
        HandlePtr mLt = maybeGet(lt);
        HandlePtr mRt = maybeGet(rt);

        return mLt && mRt && graph.haveConflictingRelation(*mLt, rel, *mRt);
    }
    template <typename X>
    bool hasComparativeRelations(X val) const {
        return has(val, comparative);
    }

    // *************************** iterators ****************************** //
    rel_iterator begin_related(V val,
                               const Relations &rels = allRelations) const;
    rel_iterator end_related(V val) const;

    RelGraph::iterator
    begin_related(Handle h, const Relations &rels = allRelations) const;
    RelGraph::iterator end_related(Handle h) const;

    rel_iterator begin_all(V val) const;
    rel_iterator end_all(V val) const;

    rel_iterator begin_lesserEqual(V val) const;
    rel_iterator end_lesserEqual(V val) const;

    plain_iterator begin() const;
    plain_iterator end() const;

    RelGraph::iterator
    begin_buckets(const Relations &rels = allRelations) const;
    RelGraph::iterator end_buckets() const;

    // ****************************** get ********************************* //
    std::vector<V> getEqual(Handle h) const;
    std::vector<V> getEqual(V val) const;

    std::vector<V> getAllRelated(V val) const;
    template <typename X>
    RelGraph::RelationsMap getAllRelated(X val) const {
        HandlePtr mH = maybeGet(val);
        assert(mH);
        return graph.getRelated(*mH, allRelations);
    }
    std::vector<V> getAllValues() const;

    std::vector<V> getDirectlyLesser(V val) const;
    std::vector<V> getDirectlyGreater(V val) const;

    std::pair<C, Relations> getLowerBound(V val) const;
    std::pair<C, Relations> getUpperBound(V val) const;

    C getLesserEqualBound(V val) const;
    C getGreaterEqualBound(V val) const;

    std::vector<V> getValsByPtr(V from) const;

    std::set<std::pair<std::vector<V>, std::vector<V>>> getAllLoads() const;

    // ************************** placeholder ***************************** //
    Handle newPlaceholderBucket();
    void erasePlaceholderBucket(Handle id);

    // ***************************** other ******************************** //
    void merge(const VR &other, Relations relations = allRelations);
    void updateChanged(bool ch) { changed |= ch; }
    void unsetChanged() { changed = false; }
    bool holdsAnyRelations() const;
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_VR_H_