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
};

class RelationsGraph {
  public:
    using BucketSet = Bucket::BucketSet;

  private:
    /*****************************************************************/
    /*                    begin iterator stuff                       */
    /*****************************************************************/
    using SetIterator = typename BucketSet::iterator;
    using RelationIndex = size_t;

  public:
    class RelationEdge {
        template <typename V>
        friend class EdgeIterator;

        Bucket &bucket;
        const SetIterator bucketIt;
        const RelationIndex relIndex;

      public:
        RelationEdge(Bucket &b, SetIterator i, RelationIndex rel)
                : bucket(b), bucketIt(i), relIndex(rel) {}

        Bucket &from() { return bucket; }
        Bucket &to() { return *bucketIt; }
        RelationType rel() const { return toRelation(relIndex); }

        friend bool operator==(const RelationEdge &lt, const RelationEdge &rt) {
            return &lt.bucket == &rt.bucket && lt.bucketIt == rt.bucketIt &&
                   lt.relIndex == rt.relIndex;
        }

        friend bool operator!=(const RelationEdge &lt, const RelationEdge &rt) {
            return !(lt == rt);
        }
    };

  private:
    template <typename V>
    class EdgeIterator {
        std::stack<RelationEdge> stack;
        V visited;

        const RelationBits allowedEdges;

        bool nextViableEdge(const Bucket &bucket, SetIterator &bucketIt,
                            RelationIndex &relIndex,
                            const Bucket *skip = nullptr) const;

      public:
        EdgeIterator() = default;
        EdgeIterator(V v) : visited(v) {}
        EdgeIterator(Bucket &start, V v, const RelationBits &a)
                : visited(v), allowedEdges(a) {
            assert(!allowedEdges.empty());
            if (visited.find(start) != visited.end())
                return;

            visited.emplace(start);

            RelationIndex relIndex = 0;
            SetIterator bucketIt =
                    start.relatedBuckets.at(toRelation(relIndex)).begin();
            if (nextViableEdge(start, bucketIt, relIndex))
                stack.emplace(start, bucketIt, relIndex);
        }

        friend bool operator==(const EdgeIterator &lt, const EdgeIterator &rt) {
            return lt.stack == rt.stack;
        }

        friend bool operator!=(const EdgeIterator &lt, const EdgeIterator &rt) {
            return !(lt == rt);
        }

        const RelationEdge &operator*() const { return stack.top(); }
        RelationEdge &operator*() { return stack.top(); }
        const RelationEdge *operator->() const { return &stack.top(); }
        RelationEdge *operator->() { return &stack.top(); }

        EdgeIterator &operator++() {
            RelationEdge top = stack.top();
            stack.pop();

            Bucket &from = top.from();
            Bucket &to = top.to();

            // plan return to next successor of "from" bucket
            SetIterator nextBucketIt = top.bucketIt;
            RelationIndex nextRelIndex = top.relIndex;
            if (!nextViableEdge(from, ++nextBucketIt, nextRelIndex))
                return *this;
            stack.emplace(from, nextBucketIt, nextRelIndex);

            if (visited.find(to) != visited.end())
                return *this;
            visited.emplace(to);

            // plan visit to first successor of "to" bucket
            RelationIndex newRelIndex = 0;
            SetIterator newBucketIt =
                    to.relatedBuckets[toRelation(newRelIndex)].begin();
            if (nextViableEdge(to, newBucketIt, newRelIndex, &from))
                stack.emplace(to, newBucketIt, newRelIndex);
            return *this;
        }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        void skipSuccessors() { stack.pop(); }
    };

    /*****************************************************************/
    /*                      end iterator stuff                       */
    /*****************************************************************/

    std::set<std::unique_ptr<Bucket>> buckets;

    void setEqual(Bucket &to, Bucket &from) {
        to.merge(from);
        from.disconnect();

        for (const auto &bucketPtr : buckets) {
            if (bucketPtr.get() == &from) {
                buckets.erase(bucketPtr);
                return;
            }
        }
    }

  public:
    RelationBits relationsBetween(Bucket &lt, Bucket &rt) const {
        RelationsMap result = getRelated(rt, allRelations);
        return result[lt];
    }

    using iterator = EdgeIterator<BucketSet>;
    using nested_iterator = EdgeIterator<BucketSet &>;
    using RelationsMap = std::map<std::reference_wrapper<Bucket>, RelationBits>;

    iterator begin(Bucket &start, const RelationBits &relations) const {
        return iterator(start, {}, relations);
    }

    iterator begin() const {
        if (!buckets.empty())
            return begin(**buckets.begin(), allRelations);
        return end();
    }

    iterator end() const { return iterator(); }

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

    bool areEqual(T lt, T rt) { return root(lt) == root(lt); }

    void setEqual(T to, T from) {
        size_t old = root(from);
        nodes[old] = root(to);
        rootBuckets.erase(old);
    }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
