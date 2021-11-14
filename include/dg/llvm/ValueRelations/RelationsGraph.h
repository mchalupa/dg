#ifndef DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
#define DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stack>

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

    // ************** bitset stuff **************
    Relations() = default;
    explicit Relations(unsigned long long val) : bits(val) {}

    bool has(Type type) const { return bits[type]; }
    Relations &set(Type t, bool v = true) {
        bits.set(t, v);
        return *this;
    }

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

struct Bucket {
    using BucketSet = std::set<std::reference_wrapper<Bucket>>;
    using ConstBucketSet = std::set<std::reference_wrapper<const Bucket>>;
    const size_t id;

    class RelationEdge {
        std::reference_wrapper<const Bucket> _from;
        Relations::Type _rel;
        std::reference_wrapper<const Bucket> _to;

        friend bool operator==(const RelationEdge &lt, const RelationEdge &rt) {
            return lt.from() == rt.from() && lt.rel() == rt.rel() &&
                   lt.to() == rt.to();
        }

        friend bool operator!=(const RelationEdge &lt, const RelationEdge &rt) {
            return !(lt == rt);
        }

        // purely for placement in set
        friend bool operator<(const RelationEdge &lt, const RelationEdge &rt) {
            if (lt.from() != rt.from())
                return lt.from() < rt.from();
            if (lt.rel() != rt.rel())
                return lt.rel() < rt.rel();
            return lt.to() < rt.to();
        }

      public:
        RelationEdge(const Bucket &f, Relations::Type r, const Bucket &t)
                : _from(f), _rel(r), _to(t) {}

        const Bucket &from() const { return _from; }
        Relations::Type rel() const { return _rel; }
        const Bucket &to() const { return _to; }

#ifndef NDEBUG
        friend std::ostream &operator<<(std::ostream &out,
                                        const RelationEdge &edge) {
            out << edge.from().id << " " << edge.rel() << " " << edge.to().id;
            return out;
        }
#endif
    };

    class DirectRelIterator {
        using SetIterator = typename BucketSet::const_iterator;
        using RelationIterator = decltype(Relations::all)::const_iterator;

        RelationIterator relationIt;
        SetIterator bucketIt;

        RelationEdge current;

        const BucketSet &relationSet() const {
            return current.from().relatedBuckets[*relationIt];
        }

        void updateCurrent() {
            current = RelationEdge(current.from(), *relationIt, *bucketIt);
        }

      public:
        // for end iterator
        DirectRelIterator(const Bucket &b, RelationIterator r)
                : relationIt(r), current(b, Relations::EQ, b) {}
        // for begin iterator
        DirectRelIterator(const Bucket &b)
                : relationIt(Relations::all.begin()),
                  bucketIt(b.relatedBuckets[*relationIt].begin()),
                  current(b, *relationIt, *bucketIt) {
            nextViableEdge();
        }

        bool nextViableEdge() {
            while (bucketIt == relationSet().end()) {
                ++relationIt;
                if (relationIt == Relations::all.end())
                    return false;
                bucketIt = relationSet().begin();
            }
            updateCurrent();
            return true;
        }

        DirectRelIterator &inc() {
            ++bucketIt;
            return *this;
        }
        DirectRelIterator &operator++() {
            ++bucketIt;
            nextViableEdge();
            return *this;
        }
        DirectRelIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        friend bool operator==(const DirectRelIterator &lt,
                               const DirectRelIterator &rt) {
            return lt.current.from() == rt.current.from() &&
                   lt.relationIt ==
                           rt.relationIt; // && lt.bucketIt == rt.bucketIt;
        }
        friend bool operator!=(const DirectRelIterator &lt,
                               const DirectRelIterator &rt) {
            return !(lt == rt);
        }

        const RelationEdge &operator*() const { return current; }
        const RelationEdge *operator->() const { return &current; }

#ifndef NDEBUG
        friend std::ostream &operator<<(std::ostream &out,
                                        const DirectRelIterator &it) {
            out << *it;
            return out;
        }
#endif
    };

  private:
    // R -> { a } such that (this, a) \in R (e.g. LE -> { a } such that this LE
    // a)
    std::array<BucketSet, Relations::total> relatedBuckets;

    // purely for storing in a set
    friend bool operator<(const Bucket &lt, const Bucket &rt) {
        return lt.id < rt.id;
    }

    friend class RelationsGraph;

    Bucket(size_t i) : id(i) { relatedBuckets[Relations::EQ].emplace(*this); }

    void merge(const Bucket &other) {
        for (Relations::Type type : Relations::all) {
            if (type == Relations::EQ)
                continue;
            for (Bucket &related : other.relatedBuckets[type]) {
                if (related != *this)
                    setRelated(*this, type, related);
            }
        }
    }

    void disconnect() {
        for (Relations::Type type : Relations::all) {
            if (type == Relations::EQ) {
                assert(relatedBuckets[type].size() == 1);
                relatedBuckets[type].clear();
            }
            for (auto it = relatedBuckets[type].begin();
                 it != relatedBuckets[type].end();
                 /*incremented by erase*/) {
                it->get().relatedBuckets[Relations::inverted(type)].erase(
                        *this);
                it = relatedBuckets[type].erase(it);
            }
        }
        assert(!hasAnyRelation());
    }

    friend void setRelated(Bucket &lt, Relations::Type type, Bucket &rt) {
        assert(lt != rt && "no reflexive relations");
        lt.relatedBuckets[type].emplace(rt);
        rt.relatedBuckets[Relations::inverted(type)].emplace(lt);
    }

    friend bool unsetRelated(Bucket &lt, Relations::Type type, Bucket &rt) {
        auto &ltRelated = lt.relatedBuckets[type];
        auto &rtRelated = rt.relatedBuckets[Relations::inverted(type)];

        auto found = ltRelated.find(rt);
        if (found == ltRelated.end()) {
            assert(rtRelated.find(lt) == rtRelated.end());
            return false;
        }

        ltRelated.erase(found);
        rtRelated.erase(lt);
        return true;
    }

    bool unset(Relations::Type rel) {
        bool changed = false;
        for (Bucket &other : relatedBuckets[rel]) {
            changed |= unsetRelated(*this, rel, other);
        }
        return changed;
    }

    bool unset(const Relations &rels) {
        bool changed = false;
        for (Relations::Type rel : Relations::all) {
            if (rels.has(rel))
                changed |= unset(rel);
        }
        return changed;
    }

  public:
    const Bucket &getRelated(Relations::Type type) const {
        assert(!relatedBuckets[type].empty());
        return *relatedBuckets[type].begin();
    }

    bool hasRelation(Relations::Type type) const {
        return !relatedBuckets[type].empty();
    }

    bool hasAnyRelation(Relations rels) const {
        for (Relations::Type rel : Relations::all) {
            if (rels.has(rel) && hasRelation(rel))
                return true;
        }
        return false;
    }

    bool hasAnyRelation() const {
        return hasAnyRelation(
                Relations(allRelations).set(Relations::EQ, false));
    }

    friend bool operator==(const Bucket &lt, const Bucket &rt) {
        return lt.id == rt.id;
    }

    friend bool operator!=(const Bucket &lt, const Bucket &rt) {
        return !(lt == rt);
    }

    /********************** begin iterator stuff *********************/

    class EdgeIterator {
        std::vector<DirectRelIterator> stack;
        std::reference_wrapper<ConstBucketSet> visited;

      public:
        Relations allowedEdges;
        bool undirectedOnly;
        bool relationsFocused;

      private:
        bool isInvertedEdge() const {
            return stack.size() >= 2 &&
                   std::any_of(std::next(stack.rbegin()), stack.rend(),
                               [&topTo = stack.back()->to()](
                                       const DirectRelIterator &it) {
                                   return it->from() == topTo;
                               });
        }

        bool shouldFollowThrough() const {
            if (stack.size() < 2)
                return true;
            Relations::Type prev = (*std::next(stack.rbegin()))->rel();
            return Relations::transitiveOver(prev, stack.back()->rel());
        }

        bool nextViableTopEdge() {
            while (stack.back().nextViableEdge()) {
                if (allowedEdges.has(stack.back()->rel()) &&
                    (!undirectedOnly || !isInvertedEdge()) &&
                    (!relationsFocused || shouldFollowThrough()))
                    return true;
                stack.back().inc();
            }
            return false;
        }

        void nextViableEdge() {
            while (!stack.empty() && !nextViableTopEdge()) {
                stack.pop_back();
            }
        }

      public:
        // for end iterator
        EdgeIterator(ConstBucketSet &v) : visited(v) {}
        // for begin iterator
        EdgeIterator(const Bucket &start, ConstBucketSet &v, const Relations &a,
                     bool u, bool r)
                : visited(v), allowedEdges(a), undirectedOnly(u),
                  relationsFocused(r) {
            assert(start.relatedBuckets.begin() != start.relatedBuckets.end() &&
                   "at least one relation");
            if (visited.get().find(start) != visited.get().end())
                return;

            visited.get().emplace(start);
            stack.emplace_back(start);
            nextViableEdge();
        }

        friend bool operator==(const EdgeIterator &lt, const EdgeIterator &rt) {
            return lt.stack == rt.stack;
        }
        friend bool operator!=(const EdgeIterator &lt, const EdgeIterator &rt) {
            return !(lt == rt);
        }

        EdgeIterator &operator++() {
            DirectRelIterator current = stack.back();
            stack.pop_back();

            const Bucket &to = current->to();

            // plan return to next successor of "from" bucket
            current.inc(); // dont use ++, because incoming relation is needed
                           // on the stack
            stack.emplace_back(current);

            // plan visit to first successor of "to" bucket if unexplored so far
            if (visited.get().find(to) == visited.get().end()) {
                stack.emplace_back(to);

                if (!relationsFocused || nextViableTopEdge())
                    visited.get().emplace(to);
                else
                    stack.pop_back();
            }

            nextViableEdge();
            return *this;
        }

        EdgeIterator &skipSuccessors() {
            stack.back().inc();
            nextViableEdge();
            return *this;
        }

        void setVisited(ConstBucketSet &v) { visited = v; }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        const RelationEdge &operator*() const { return *stack.back(); }
        const RelationEdge *operator->() const { return &(*stack.back()); }
    };

  public:
    using iterator = EdgeIterator;

    iterator begin(ConstBucketSet &visited, const Relations &relations,
                   bool undirectedOnly, bool relationsFocused) const {
        return iterator(*this, visited, relations, undirectedOnly,
                        relationsFocused);
    }

    iterator begin(ConstBucketSet &visited) const {
        return begin(visited, allRelations, true, true);
    }

    iterator end(ConstBucketSet &visited) const { return iterator(visited); }

    DirectRelIterator begin() const { return DirectRelIterator(*this); }

    DirectRelIterator end() const {
        return DirectRelIterator(*this, Relations::all.end());
    }

    /*********************** end iterator stuff **********************/

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out, const Bucket &bucket) {
        out << bucket.id << " | ";
        for (Relations::Type type : Relations::all) {
            if (bucket.hasRelation(type)) {
                out << type << " - ";
                for (Bucket &related : bucket.relatedBuckets[type])
                    out << related.id
                        << (related == *bucket.relatedBuckets[type].rbegin()
                                    ? ""
                                    : ", ");
                out << "; ";
            }
        }
        return out;
    }
#endif
};

class RelationsGraph {
    using UniqueBucketSet = std::set<std::unique_ptr<Bucket>>;

    class EdgeIterator {
        using BucketIterator = UniqueBucketSet::iterator;
        Bucket::ConstBucketSet visited;

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

    UniqueBucketSet buckets;
    size_t lastId = 0;

    bool setEqual(Bucket &to, Bucket &from) {
        assert(to != from);
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

  public:
    using iterator = EdgeIterator;

    Relations relationsBetween(const Bucket &lt, const Bucket &rt) const {
        RelationsMap result = getRelated(lt, allRelations);
        return result[rt];
    }

    using RelationsMap =
            std::map<std::reference_wrapper<const Bucket>, Relations>;

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

    iterator begin() const { return begin(allRelations, true); }

    iterator begin(const Relations &relations,
                   bool undirectedOnly = true) const {
        if (!buckets.empty())
            return iterator(buckets.begin(), buckets.end(), relations,
                            undirectedOnly, false);
        return end();
    }

    iterator end() const { return iterator(buckets.end()); }

    RelationsMap getRelated(const Bucket &start, const Relations &relations,
                            bool toFirstStrict = false) const;

    bool areRelated(const Bucket &lt, Relations::Type type, const Bucket &rt,
                    Relations *maybeBetween = nullptr) const;

    bool haveConflictingRelation(const Bucket &lt, Relations::Type type,
                                 const Bucket &rt,
                                 Relations *maybeBetween = nullptr) const;

    bool addRelation(const Bucket &lt, Relations::Type type, const Bucket &rt,
                     Relations *maybeBetween = nullptr);

    const Bucket &getNewBucket() {
        auto pair = buckets.emplace(new Bucket(++lastId));
        return **pair.first;
    }

    const UniqueBucketSet &getBuckets() const { return buckets; }

    bool unset(const Relations &rels) {
        bool changed = false;
        for (auto &bucketPtr : buckets) {
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

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out,
                                    const RelationsGraph &graph) {
        out << "RELATIONS BEGIN\n";
        for (const auto &item : graph.buckets)
            out << "    " << *item << "\n";
        out << "RELATIONS END\n";
        return out;
    }
#endif
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
