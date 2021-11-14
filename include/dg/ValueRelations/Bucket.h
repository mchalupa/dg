#ifndef DG_LLVM_VALUE_RELATIONS_BUCKET_H_
#define DG_LLVM_VALUE_RELATIONS_BUCKET_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <algorithm>
#include <cassert>
#include <functional>
#include <set>
#include <type_traits>
#include <vector>

#include "Relations.h"

namespace dg {
namespace vr {

// implementation of set using vector to save memory (at the time of writing,
// saved about a quarter of total), with no time overhead in this case
template <typename T>
class VectorSet {
    std::vector<T> vec;

  public:
    using const_iterator = typename std::vector<T>::const_iterator;
    using iterator = typename std::vector<T>::iterator;
    using const_reverse_iterator =
            typename std::vector<T>::const_reverse_iterator;

    VectorSet() = default;

    template <typename S>
    VectorSet(std::initializer_list<S> lst) : vec(std::move(lst)) {}

    template <typename S>
    iterator find(const S &val) {
        return std::find(vec.begin(), vec.end(), val);
    }
    template <typename S>
    const_iterator find(const S &val) const {
        return std::find(vec.begin(), vec.end(), val);
    }

    template <typename S>
    bool contains(const S &val) const {
        return find(val) != vec.end();
    }

    template <typename... S>
    void emplace(S &&...vals) {
        T val(std::forward<S>(vals)...);
        if (!contains(val))
            vec.emplace_back(std::move(val));
    }

    template <typename... S>
    void sure_emplace(S &&...vals) {
        vec.emplace_back(std::forward<S>(vals)...);
    }

    template <typename It, typename X = typename It::iterator_category>
    iterator erase(const It &it) {
        return vec.erase(it);
    }

    template <typename S,
              typename X = decltype(std::declval<S &>() == std::declval<T &>())>
    bool erase(const S &val) {
        auto it = find(val);
        if (it == vec.end())
            return false;
        vec.erase(it);
        return true;
    }

    void clear() { vec.clear(); }
    size_t size() const { return vec.size(); }
    bool empty() const { return vec.empty(); }

    iterator begin() { return vec.begin(); }
    iterator end() { return vec.end(); }
    const_iterator begin() const { return vec.begin(); }
    const_iterator end() const { return vec.end(); }

    iterator rbegin() { return vec.rbegin(); }
    iterator rend() { return vec.rend(); }
    const_reverse_iterator rbegin() const { return vec.rbegin(); }
    const_reverse_iterator rend() const { return vec.rend(); }

    T any() const {
        assert(!empty());
        return vec[0];
    }
};

struct Bucket {
    using ConstBucketSet = VectorSet<std::reference_wrapper<const Bucket>>;
    using BucketSet = VectorSet<std::reference_wrapper<Bucket>>;
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

        RelationEdge inverted() const {
            return {_to, Relations::inverted(_rel), _from};
        }

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
        friend class EdgeIterator;

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

    template <typename T>
    friend class RelationsGraph;

    Bucket(size_t i) : id(i) { relatedBuckets[Relations::EQ].emplace(*this); }

    void merge(const Bucket &other) {
        if (*this == other)
            return;
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
                if (*this != *it)
                    it->get().relatedBuckets[Relations::inverted(type)].erase(
                            *this);
                it = relatedBuckets[type].erase(it);
            }
        }
        assert(!hasAnyRelation());
    }

    friend void setRelated(Bucket &lt, Relations::Type type, Bucket &rt) {
        assert(lt != rt || !comparative.has(type));
        lt.relatedBuckets[type].emplace(rt);
        rt.relatedBuckets[Relations::inverted(type)].emplace(lt);
    }

    friend bool unsetRelated(Bucket &lt, Relations::Type type, Bucket &rt) {
        assert(type != Relations::EQ);
        if (lt == rt) {
            return lt.relatedBuckets[type].erase(rt);
        }
        auto &ltRelated = lt.relatedBuckets[type];
        auto &rtRelated = rt.relatedBuckets[Relations::inverted(type)];

        auto found = ltRelated.find(rt);
        if (found == ltRelated.end()) {
            assert(!rtRelated.contains(lt));
            return false;
        }

        ltRelated.erase(found);
        rtRelated.erase(lt);
        return true;
    }

    bool unset(Relations::Type rel) {
        bool changed = false;
        BucketSet related = relatedBuckets[rel];
        for (Bucket &other : related) {
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
    Bucket(const Bucket &) = delete;

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

    struct EdgeIterator {
        Relations allowedEdges;
        bool undirectedOnly = false;
        bool relationsFocused = false;
        using Visited = std::set<RelationEdge>;

      private:
        std::vector<DirectRelIterator> stack;
        std::reference_wrapper<Visited> visited;

        bool shouldFollowThrough() const {
            if (stack.size() < 2)
                return true;
            Relations::Type prev = (*std::next(stack.rbegin()))->rel();
            return Relations::transitiveOver(prev, stack.back()->rel());
        }

        bool isViable() {
            return visited.get().find(*stack.back()) == visited.get().end() &&
                   allowedEdges.has(stack.back()->rel()) &&
                   (!relationsFocused || shouldFollowThrough());
        }

        bool nextViableTopEdge() {
            while (stack.back().nextViableEdge()) {
                if (isViable()) {
                    visited.get().emplace(*stack.back());
                    if (undirectedOnly)
                        visited.get().emplace(stack.back()->inverted());
                    return true;
                }
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
        EdgeIterator(Visited &v) : visited(v) {}
        // for begin iterator
        EdgeIterator(const Bucket &start, Visited &v, const Relations &a,
                     bool u, bool r)
                : allowedEdges(a), undirectedOnly(u), relationsFocused(r),
                  visited(v) {
            assert(start.relatedBuckets.begin() != start.relatedBuckets.end() &&
                   "at least one relation");

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
            stack.emplace_back(to);
            nextViableEdge();

            return *this;
        }

        EdgeIterator &skipSuccessors() {
            stack.back().inc();
            nextViableEdge();
            return *this;
        }

        void setVisited(Visited &v) { visited = v; }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        const RelationEdge &operator*() const { return *stack.back(); }
        const RelationEdge *operator->() const { return &(*stack.back()); }
    };

    using iterator = EdgeIterator;

    iterator begin(iterator::Visited &visited, const Relations &relations,
                   bool undirectedOnly, bool relationsFocused) const {
        return iterator(*this, visited, relations, undirectedOnly,
                        relationsFocused);
    }

    iterator begin(iterator::Visited &visited) const {
        return begin(visited, allRelations, true, true);
    }

    static iterator end(iterator::Visited &visited) {
        return iterator(visited);
    }

    DirectRelIterator begin() const { return {*this}; }

    DirectRelIterator end() const { return {*this, Relations::all.end()}; }

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

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_BUCKET_H_
