#ifndef DG_LLVM_VALUE_RELATIONS_VALUE_RELATIONS_H_
#define DG_LLVM_VALUE_RELATIONS_VALUE_RELATIONS_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>

#include <map>

#ifndef NDEBUG
#include "getValName.h"
#include <iostream>
#endif

#include "dg/ValueRelations/RelationsGraph.h"

namespace dg {
namespace vr {

struct ValueRelations {
    using RelGraph = RelationsGraph<ValueRelations>;
    using Handle = const Bucket &;
    using HandlePtr = const Bucket *;
    using BRef = std::reference_wrapper<const Bucket>;
    using RelationsMap = RelGraph::RelationsMap;
    using V = const llvm::Value *;
    using C = const llvm::ConstantInt *;

    using ValToBucket = std::map<V, BRef>;
    using BucketToVals = std::map<BRef, VectorSet<V>>;

  private:
    using BareC = llvm::ConstantInt;

    template <typename T>
    friend class RelationsGraph;
    friend class ValueIterator;

    RelGraph graph;

    ValToBucket valToBucket;
    BucketToVals bucketToVals;
    std::vector<bool> validAreas;

    bool changed = false;

    // ****************************** get ********************************* //
    static HandlePtr maybeGet(Handle h) { return &h; }
    HandlePtr maybeGet(V val) const;

    static std::pair<BRef, bool> get(Handle h) { return {h, false}; }
    std::pair<BRef, bool> get(size_t id);
    std::pair<BRef, bool> get(V val);

    V getAny(Handle h) const;
    ValueRelations::C getAnyConst(Handle h) const;

    std::pair<ValueRelations::C, Relations> getBound(Handle h,
                                                     Relations rel) const;
    std::pair<ValueRelations::C, Relations> getBound(V val,
                                                     Relations rel) const;

    static HandlePtr getHandleByPtr(Handle h);

    // ************************* general unset **************************** //
    void unset(Relations rels) {
        assert(!rels.has(Relations::EQ));
        bool ch = graph.unset(rels);
        updateChanged(ch);
    }
    void unset(Relations::Type rel) { unset(Relations().set(rel)); }
    template <typename X>
    void unset(const X &val, Relations rels) {
        if (HandlePtr mH = maybeGet(val)) {
            bool ch = graph.unset(*mH, rels);
            updateChanged(ch);
        }
    }
    template <typename X>
    void unset(const X &val, Relations::Type rel) {
        unset(val, Relations().set(rel));
    }

    // *********************** general between *************************** //
    Relations _between(Handle lt, Handle rt) const;
    Relations _between(Handle lt, V rt) const;
    Relations _between(Handle lt, C rt) const;
    Relations _between(V lt, Handle rt) const;
    Relations _between(C lt, Handle rt) const;
    Relations _between(V lt, V rt) const;
    template <typename X>
    Relations _between(size_t lt, const X &rt) const {
        HandlePtr mH = getBorderH(lt);
        return mH ? _between(*mH, rt) : Relations();
    }
    Relations _between(Handle lt, size_t rt) const;
    Relations _between(V lt, size_t rt) const;

    // *************************** iterators ***************************** //
    class RelatedValueIterator {
        using RelatedIterator = RelationsMap::iterator;
        using ValIterator = typename VectorSet<V>::const_iterator;

        const ValueRelations &vr;

        RelationsMap related;
        RelatedIterator bucketIt;
        ValIterator valueIt;

        std::pair<V, Relations> current;

        bool isEnd = false;

        const VectorSet<V> &getCurrentEqual() const {
            return vr.bucketToVals.find(bucketIt->first)->second;
        }

        void updateCurrent() {
            current = std::make_pair(*valueIt, bucketIt->second);
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
            updateCurrent();
        }

      public:
        using value_type = decltype(current);
        using difference_type = int64_t;
        using iterator_category = std::input_iterator_tag;

        using reference = value_type &;
        using pointer = value_type *;

        // for end iterator
        RelatedValueIterator(const ValueRelations &v) : vr(v), isEnd(true) {}
        // for begin iterator
        RelatedValueIterator(const ValueRelations &v, Handle start,
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

        reference operator*() { return current; }
        pointer operator->() { return &current; }
    };

    class PlainValueIterator {
        using BucketIterator = std::map<BRef, VectorSet<V>>::const_iterator;
        using ValIterator = VectorSet<V>::const_iterator;

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
    HandlePtr getCorresponding(const ValueRelations &other, Handle otherH,
                               const VectorSet<V> &otherEqual);
    HandlePtr getCorresponding(const ValueRelations &other, Handle otherH);
    HandlePtr getAndMerge(const ValueRelations &other, Handle otherH);
    void add(V val, Handle h, VectorSet<V> &vals);
    std::pair<BRef, bool> add(V val, Handle h);
    void areMerged(Handle to, Handle from);
    void updateChanged(bool ch) { changed |= ch; }

  public:
    ValueRelations() : graph(*this) {}
    ValueRelations(const ValueRelations &) = delete;

    using rel_iterator = RelatedValueIterator;
    using plain_iterator = PlainValueIterator;

    // ****************************** get ********************************* //
    HandlePtr getHandle(V val) const;

    template <typename I>
    const I *getInstance(V v) const {
        HandlePtr mH = maybeGet(v);
        if (!mH)
            return llvm::dyn_cast<I>(v);
        return getInstance<I>(*mH);
    }

    template <typename I>
    const I *getInstance(Handle h) const {
        for (const auto *val : getEqual(h)) {
            if (const auto *inst = llvm::dyn_cast<I>(val))
                return inst;
        }
        return nullptr;
    }

    // ****************************** set ********************************* //
    template <typename X, typename Y>
    void set(const X &lt, Relations::Type rel, const Y &rt) {
        auto ltHPair = get(lt);
        auto rtHPair = get(rt);
        if (rtHPair.second) {
            ltHPair = get(lt);
            assert(!ltHPair.second);
        }
        bool ch = graph.addRelation(ltHPair.first, rel, rtHPair.first);
        updateChanged(ch);
    }
    template <typename X, typename Y>
    void set(const X &lt, Relations rels, const Y &rt) {
        Relations::Type rel = rels.get();
        set(lt, rel, rt);
        Relations other = rels & Relations().ult().ule().ugt().uge();
        if (other.any() && !Relations().set(rel).addImplied().has(other.get()))
            set(lt, other.get(), rt);
    }
    template <typename X, typename Y>
    void setEqual(const X &lt, const Y &rt) {
        set(lt, Relations::EQ, rt);
    }
    template <typename X, typename Y>
    void setNonEqual(const X &lt, const Y &rt) {
        set(lt, Relations::NE, rt);
    }
    template <typename X, typename Y>
    void setLesser(const X &lt, const Y &rt) {
        set(lt, Relations::SLT, rt);
    }
    template <typename X, typename Y>
    void setLesserEqual(const X &lt, const Y &rt) {
        set(lt, Relations::SLE, rt);
    }
    template <typename X, typename Y>
    void setLoad(const X &from, const Y &to) {
        set(from, Relations::PT, to);
    }

    // ***************************** unset ******************************** //
    template <typename X>
    void unsetAllLoadsByPtr(const X &from) {
        unset(from, Relations::PT);
    }
    void unsetAllLoads() { unset(Relations::PT); }
    template <typename X>
    void unsetComparativeRelations(const X &val) {
        unset(val, Relations(comparative).set(Relations::EQ, false));
    }

    // ******************************* is ********************************** //
    template <typename X, typename Y>
    Relations between(const X &lt, const Y &rt) const {
        return _between(lt, rt);
    }
    template <typename X, typename Y>
    bool are(const X &lt, Relations rels, const Y &rt) const {
        return (_between(lt, rt) & rels) == rels;
    }
    template <typename X, typename Y>
    bool are(const X &lt, Relations::Type rel, const Y &rt) const {
        return _between(lt, rt).has(rel);
    }
    template <typename X, typename Y>
    bool isEqual(const X &lt, const Y &rt) const {
        return are(lt, Relations::EQ, rt);
    }
    template <typename X, typename Y>
    bool isNonEqual(const X &lt, const Y &rt) const {
        return are(lt, Relations::NE, rt);
    }
    template <typename X, typename Y>
    bool isLesser(const X &lt, const Y &rt) const {
        return are(lt, Relations::SLT, rt);
    }
    template <typename X, typename Y>
    bool isLesserEqual(const X &lt, const Y &rt) const {
        return are(lt, Relations::SLE, rt);
    }
    template <typename X, typename Y>
    bool isLoad(const X &from, const Y &to) const {
        return are(from, Relations::PT, to);
    }

    // ****************************** has ********************************* //
    template <typename X>
    bool has(const X &val, Relations::Type rel) const {
        HandlePtr mVal = maybeGet(val);
        return mVal && mVal->hasRelation(rel);
    }
    template <typename X>
    bool has(const X &val, Relations rels) const {
        HandlePtr mVal = maybeGet(val);
        return mVal && ((rels.has(Relations::EQ) &&
                         bucketToVals.find(*mVal)->second.size() > 1) ||
                        mVal->hasAnyRelation(rels.set(Relations::EQ, false)));
    }
    template <typename X>
    bool hasLoad(const X &from) const {
        return has(from, Relations::PT);
    }
    template <typename X, typename Y>
    bool haveConflictingRelations(const X &lt, Relations::Type rel,
                                  const Y &rt) const {
        HandlePtr mLt = maybeGet(lt);
        HandlePtr mRt = maybeGet(rt);

        return mLt && mRt && graph.haveConflictingRelation(*mLt, rel, *mRt);
    }
    template <typename X, typename Y>
    bool hasConflictingRelation(const X &lt, const Y &rt,
                                Relations::Type rel) const {
        return haveConflictingRelations(lt, rel, rt);
    }
    template <typename X>
    bool hasComparativeRelations(const X &val) const {
        return has(val, comparative);
    }
    template <typename X>
    bool hasAnyRelation(const X &val) const {
        return has(val, allRelations);
    }
    template <typename X>
    bool hasEqual(const X &val) const {
        return has(val, Relations().eq());
    }
    template <typename X>
    bool contains(const X &val) const {
        return maybeGet(val);
    }

    // *************************** iterators ****************************** //
    rel_iterator begin_related(V val, const Relations &rels = restricted) const;
    rel_iterator end_related(V val) const;

    RelGraph::iterator begin_related(Handle h,
                                     const Relations &rels = restricted) const;
    RelGraph::iterator end_related(Handle h) const;

    plain_iterator begin() const;
    plain_iterator end() const;

    RelGraph::iterator
    begin_buckets(const Relations &rels = allRelations) const;
    RelGraph::iterator end_buckets() const;

    const ValToBucket &getValToBucket() const { return valToBucket; }
    const BucketToVals &getBucketToVals() const { return bucketToVals; }

    // ****************************** get ********************************* //
    const VectorSet<V> &getEqual(Handle h) const;
    VectorSet<V> getEqual(V val) const;

    template <typename X>
    RelGraph::RelationsMap getAllRelated(const X &val) const {
        return getRelated(val, allRelations);
    }
    template <typename X>
    RelGraph::RelationsMap getRelated(const X &val,
                                      const Relations &rels) const {
        HandlePtr mH = maybeGet(val);
        if (!mH)
            return {};
        return graph.getRelated(*mH, rels);
    }

    std::vector<V> getDirectlyRelated(V val, const Relations &rels) const;

    template <typename X>
    std::pair<C, Relations> getBound(const X &val, Relations::Type rel) const {
        assert(nonStrict.has(rel));
        return getBound(val, Relations().set(rel));
    }
    template <typename X>
    std::pair<C, Relations> getLowerBound(const X &val) const {
        return getBound(val, Relations().sge());
    }
    template <typename X>
    std::pair<C, Relations> getUpperBound(const X &val) const {
        return getBound(val, Relations().sle());
    }

    C getLesserEqualBound(V val) const;
    C getGreaterEqualBound(V val) const;

    VectorSet<V> getValsByPtr(V from) const;

    template <typename X>
    Handle getPointedTo(const X &from) const {
        HandlePtr mH = maybeGet(from);
        assert(mH);
        return mH->getRelated(Relations::PT);
    }

    std::vector<bool> &getValidAreas() { return validAreas; }
    const std::vector<bool> &getValidAreas() const { return validAreas; }

    // ************************** placeholder ***************************** //
    Handle newBorderBucket(size_t id) {
        Handle h = graph.getBorderBucket(id);
        bucketToVals[h];
        return h;
    }

    template <typename X>
    Handle newPlaceholderBucket(const X &from) {
        HandlePtr mH = maybeGet(from);
        if (mH && mH->hasRelation(Relations::PT)) {
            return mH->getRelated(Relations::PT);
        }

        Handle h = graph.getNewBucket();
        bucketToVals[h];
        return h;
    }
    void erasePlaceholderBucket(Handle h);

    // ***************************** other ******************************** //
    static bool compare(C lt, Relations::Type rel, C rt);
    static bool compare(C lt, Relations rels, C rt);
    static Relations compare(C lt, C rt);
    bool merge(const ValueRelations &other, Relations relations = allRelations);
    bool unsetChanged() {
        bool old = changed;
        changed = false;
        return old;
    }
    bool holdsAnyRelations() const;

    HandlePtr getBorderH(size_t id) const;
    size_t getBorderId(Handle h) const;

#ifndef NDEBUG
    void dump(ValueRelations::Handle h, std::ostream &out = std::cerr) const;
    void dotDump(std::ostream &out = std::cerr) const;
    friend std::ostream &operator<<(std::ostream &out,
                                    const ValueRelations &vr);
#endif
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_VALUE_RELATIONS_H_
