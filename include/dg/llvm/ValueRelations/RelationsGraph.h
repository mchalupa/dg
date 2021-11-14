#ifndef DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
#define DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_

#ifndef NDEBUG
#include "getValName.h"
#include <iostream>
#endif

#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <vector>

namespace dg {
namespace vr {

enum class RelationType { EQ, NE, LE, LT, GE, GT, PT, PF };
using RelationBits = std::bitset<8>;

RelationType toRelation(size_t i);
size_t toInt(RelationType t);
RelationType inverted(RelationType type);
RelationType negated(RelationType type);
RelationBits conflicting(RelationType type);
bool transitive(RelationType type);
const RelationBits allRelations;
const RelationBits undirected;

#ifndef NDEBUG
void dumpRelation(RelationType r);
#endif

class Bucket {
  public:
    using BucketSet = std::set<std::reference_wrapper<Bucket>>;

  private:
    // R -> { a } such that (a, this) \in R (e.g. LE -> { a } such that a LE
    // this)
    std::map<RelationType, BucketSet> relatedBuckets;

    friend class RelationsGraph;
    // purely for storing in a set
    friend bool operator<(const Bucket &lt, const Bucket &rt) {
        return &lt < &rt;
    }

  public:
    Bucket() {
        for (size_t i = 0; i < allRelations.size(); ++i)
            relatedBuckets[toRelation(i)];
    };

    void merge(const Bucket &other) {
        for (auto &pair : other.relatedBuckets) {
            for (Bucket &related : pair.second) {
                if (&related != this)
                    setRelated(related, pair.first, *this);
            }
        }
    }

    void disconnect() {
        for (auto &pair : relatedBuckets) {
            for (Bucket &related : pair.second) {
                unsetRelated(related, pair.first, *this);
            }
        }
    }

    friend void setRelated(Bucket &lt, RelationType type, Bucket &rt) {
        assert(&lt != &rt && "no reflexive relations");
        rt.relatedBuckets[type].emplace(lt);
        lt.relatedBuckets[inverted(type)].emplace(rt);
    }

    friend void unsetRelated(Bucket &lt, RelationType type, Bucket &rt) {
        rt.relatedBuckets[type].erase(lt);
        lt.relatedBuckets[inverted(type)].erase(rt);
    }

    bool hasRelation(RelationType type) const {
        auto it = relatedBuckets.find(type);
        return it != relatedBuckets.end() && !it->second.empty();
    }

    bool hasAnyRelation() const {
        for (auto &pair : relatedBuckets) {
            if (pair.second.begin() != pair.second.end())
                return true;
        }
        return false;
    }

    friend bool operator==(const Bucket &lt, const Bucket &rt) {
        return &lt == &rt;
    }

    friend bool operator!=(const Bucket &lt, const Bucket &rt) {
        return !(lt == rt);
    }

    /********************** begin iterator stuff *********************/
    class RelationEdge {
        template <typename V>
        friend class EdgeIterator;

        using SetIterator = typename BucketSet::iterator;
        using RelationIterator =
                typename std::map<RelationType, BucketSet>::iterator;

        friend class EdgeIterator;

      public:
        Bucket &bucket;
        RelationIterator relationIt;
        SetIterator bucketIt;

        RelationEdge(Bucket &b)
                : bucket(b), relationIt(b.relatedBuckets.begin()),
                  bucketIt(relationIt->second.begin()) {}

        bool nextViableEdge(const RelationBits &allowedEdges) {
            while (bucketIt == relationIt->second.end() ||
                   !allowedEdges[toInt(rel())]) {
                ++relationIt;
                if (relationIt == bucket.relatedBuckets.end())
                    return false;
                bucketIt = relationIt->second.begin();
            }
            return true;
        }

        Bucket &from() { return bucket; }
        RelationType rel() const { return relationIt->first; }
        Bucket &to() { return *bucketIt; }

        friend bool operator==(const RelationEdge &lt, const RelationEdge &rt) {
            return lt.relationIt == rt.relationIt;
        }

        friend bool operator!=(const RelationEdge &lt, const RelationEdge &rt) {
            return !(lt == rt);
        }
    };

    class EdgeIterator {
        std::stack<RelationEdge> stack;
        std::reference_wrapper<BucketSet> visited;
        RelationBits allowedEdges;

      public:
        // for end iterator
        EdgeIterator(BucketSet &v) : visited(v) {}
        // for begin iterator
        EdgeIterator(Bucket &start, BucketSet &v, const RelationBits &a)
                : visited(v), allowedEdges(a) {
            assert(start.relatedBuckets.begin() != start.relatedBuckets.end() &&
                   "at least one relation");
            if (visited.get().find(start) != visited.get().end())
                return;
            visited.get().emplace(start);

            RelationEdge current(start);
            if (current.nextViableEdge(allowedEdges))
                stack.emplace(current);
        }

        friend bool operator==(const EdgeIterator &lt, const EdgeIterator &rt) {
            return lt.stack == rt.stack;
        }

        friend bool operator!=(const EdgeIterator &lt, const EdgeIterator &rt) {
            return !(lt == rt);
        }

        EdgeIterator &operator++() {
            RelationEdge current = stack.top();
            stack.pop();

            Bucket &to = current.to();

            // plan return to next successor of "from" bucket
            ++current.bucketIt;
            if (!current.nextViableEdge(allowedEdges))
                return *this;
            stack.emplace(current);

            if (visited.get().find(to) != visited.get().end())
                return *this;
            visited.get().emplace(to);

            // plan visit to first successor of "to" bucket
            RelationEdge next(to);
            if (next.nextViableEdge(allowedEdges))
                stack.emplace(next);
            return *this;
        }

        void skipSuccessors() { stack.pop(); }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        RelationEdge &operator*() { return stack.top(); }
        RelationEdge *operator->() { return &stack.top(); }
        const RelationEdge &operator*() const { return stack.top(); }
        const RelationEdge *operator->() const { return &stack.top(); }
    };

    EdgeIterator begin(BucketSet &visited, const RelationBits &relations) {
        return EdgeIterator(*this, visited, relations);
    }

    EdgeIterator begin(BucketSet &visited) {
        return begin(visited, undirected);
    }

    EdgeIterator end(BucketSet &visited) { return EdgeIterator(visited); }

    /*********************** end iterator stuff **********************/
};

class RelationsGraph {
    using UniqueBucketSet = std::set<std::unique_ptr<Bucket>>;

    class EdgeIterator {
        using BucketIterator = UniqueBucketSet::iterator;

        BucketIterator bucketIt;
        BucketIterator endIt;
        Bucket::EdgeIterator edgeIt;

        Bucket::BucketSet visited;
        RelationBits allowedEdges;

        void nextViableEdge() {
            while (edgeIt == (*bucketIt)->end(visited)) {
                ++bucketIt;
                if (bucketIt == endIt)
                    return;
                edgeIt = (*bucketIt)->begin(visited);
            }
        }

      public:
        EdgeIterator(BucketIterator end)
                : bucketIt(end), endIt(end), edgeIt(visited) {}
        EdgeIterator(BucketIterator start, BucketIterator end,
                     const RelationBits &a)
                : bucketIt(start), endIt(end),
                  edgeIt((*bucketIt)->begin(visited)), allowedEdges(a) {
            assert(bucketIt != endIt && "at least one bucket");
            nextViableEdge();
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

        void skipSuccessors() { edgeIt.skipSuccessors(); }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        Bucket::RelationEdge &operator*() { return *edgeIt; }
        Bucket::RelationEdge *operator->() { return &(*edgeIt); }
        const Bucket::RelationEdge &operator*() const { return *edgeIt; }
        const Bucket::RelationEdge *operator->() const { return &(*edgeIt); }
    };

    //*********************** end iterator stuff **********************

    UniqueBucketSet buckets;

    void setEqual(Bucket &to, Bucket &from) {
        to.merge(from);
        from.disconnect();

        auto it = getItFor(from);
        buckets.erase(it);
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
    RelationBits relationsBetween(Bucket &lt, Bucket &rt) const {
        RelationsMap result = getRelated(rt, allRelations);
        return result[lt];
    }

    using RelationsMap = std::map<std::reference_wrapper<Bucket>, RelationBits>;

    EdgeIterator begin(Bucket &start, const RelationBits &relations) const {
        auto startIt = getItFor(start);
        auto endIt = startIt;
        return EdgeIterator(startIt, ++endIt, relations);
    }

    EdgeIterator begin(Bucket &start) const { return begin(start, undirected); }

    EdgeIterator end(Bucket &start) const {
        auto endIt = ++getItFor(start);
        return EdgeIterator(endIt);
    }

    EdgeIterator begin() const {
        if (!buckets.empty())
            return EdgeIterator(buckets.begin(), buckets.end(), undirected);
        return end();
    }

    EdgeIterator end() const { return EdgeIterator(buckets.end()); }

    RelationsMap getRelated(Bucket &start, const RelationBits &relations,
                            bool toFirstStrict = false) const;

    RelationsMap getRelated(Bucket &start, const RelationBits &relations,
                            bool toFirstStrict = false) const;

    bool areRelated(Bucket &lt, RelationType type, Bucket &rt,
                    RelationBits *maybeBetween = nullptr) const;

    bool haveConflictingRelation(Bucket &lt, RelationType type, Bucket &rt,
                                 RelationBits *maybeBetween = nullptr) const;

    void addRelation(Bucket &lt, RelationType type, Bucket &rt,
                     RelationBits *maybeBetween = nullptr);
};

template <typename T>
class BucketedValues {
    std::map<T, size_t> valueMapping;
    std::vector<size_t> nodes;
    std::map<size_t, std::reference_wrapper<Bucket>> rootBuckets;

    size_t root(T val) {
        size_t current = valueMapping.at(val);

        while (current != nodes[current]) {
            nodes[current] = nodes[nodes[current]];
            current = nodes[current];
        }
        return current;
    }

    Bucket &getBucket(size_t node) const {
        assert(rootBuckets.find(node) != rootBuckets.end());
        return rootBuckets.at(node);
    }

  public:
    bool add(T val, Bucket &bucket) {
        size_t newNode = nodes.size();
        bool inserted = valueMapping.emplace(val, newNode).second;

        if (inserted) {
            nodes.emplace_back(newNode);

            assert(rootBuckets.find(newNode) == rootBuckets.end());
            rootBuckets.emplace(newNode, bucket);
        }

        return inserted;
    }

    bool contains(T val) const {
        return valueMapping.find(val) != valueMapping.end();
    }

    Bucket &getBucket(T val) {
        size_t node = root(val);
        return getBucket(node);
    }

    bool areEqual(T lt, T rt) { return root(lt) == root(rt); }

    void setEqual(T to, T from) {
        size_t old = root(from);
        nodes[old] = root(to);
        rootBuckets.erase(old);
    }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
