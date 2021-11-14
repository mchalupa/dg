#ifndef DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
#define DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <bitset>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stack>

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

extern const RelationBits allRelations;

#ifndef NDEBUG
void dumpRelation(RelationType r);
std::ostream &operator<<(std::ostream &out, RelationType r);
#endif

class Bucket {
  public:
    using BucketSet = std::set<std::reference_wrapper<Bucket>>;

  private:
    // R -> { a } such that (a, this) \in R (e.g. LE -> { a } such that a LE
    // this)
    std::map<RelationType, BucketSet> relatedBuckets;

    // purely for storing in a set
    friend bool operator<(const Bucket &lt, const Bucket &rt) {
        return lt.id < rt.id;
    }

  public:
    const size_t id;

    Bucket(size_t i) : id(i) {
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
        assert(lt != rt && "no reflexive relations");
        rt.relatedBuckets[type].emplace(lt);
        lt.relatedBuckets[inverted(type)].emplace(rt);
    }

    friend void unsetRelated(Bucket &lt, RelationType type, Bucket &rt) {
        rt.relatedBuckets[type].erase(lt);
        lt.relatedBuckets[inverted(type)].erase(rt);
    }

    Bucket &getRelated(RelationType type) {
        assert(!relatedBuckets[type].empty());
        return *relatedBuckets[type].begin();
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
        return lt.id == rt.id;
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

        bool nextViableEdge() {
            while (bucketIt == relationIt->second.end()) {
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
            return lt.bucket == rt.bucket && lt.relationIt == rt.relationIt &&
                   lt.bucketIt == rt.bucketIt;
        }

        friend bool operator!=(const RelationEdge &lt, const RelationEdge &rt) {
            return !(lt == rt);
        }

#ifndef NDEBUG
        friend std::ostream &operator<<(std::ostream &out,
                                        const RelationEdge &edge) {
            auto nEdge = const_cast<RelationEdge &>(edge);
            out << nEdge.to().id << " " << edge.rel() << " " << nEdge.from().id;
            return out;
        }
#endif
    };

    class EdgeIterator {
        std::vector<RelationEdge> stack;
        std::reference_wrapper<BucketSet> visited;
        RelationBits allowedEdges;

        bool undirectedOnly;

        bool isInvertedEdge() {
            return stack.size() >= 2 &&
                   stack.back().to() == std::next(stack.rbegin())->from();
        }

        void nextViableEdge() {
            while (!stack.empty()) {
                // find next viable edge from the top "from" bucket
                while (stack.back().nextViableEdge()) {
                    if (allowedEdges[toInt(stack.back().rel())] &&
                        (!undirectedOnly || !isInvertedEdge()))
                        return;
                    ++stack.back().bucketIt;
                }
                // no more viable edges from the top "from" bucket
                stack.pop_back();
            }
        }

      public:
        // for end iterator
        EdgeIterator(BucketSet &v) : visited(v) {}
        // for begin iterator
        EdgeIterator(Bucket &start, BucketSet &v, const RelationBits &a, bool u)
                : visited(v), allowedEdges(a), undirectedOnly(u) {
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
            RelationEdge current = stack.back();
            stack.pop_back();

            Bucket &to = current.to();

            // plan return to next successor of "from" bucket
            ++current.bucketIt;
            stack.emplace_back(current);

            // plan visit to first successor of "to" bucket if unexplored so far
            if (visited.get().find(to) == visited.get().end()) {
                visited.get().emplace(to);

                stack.emplace_back(to);
            }

            nextViableEdge();

            return *this;
        }

        void skipSuccessors() { stack.pop_back(); }

        void setVisited(BucketSet &v) { visited = v; }

        EdgeIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        RelationEdge &operator*() { return stack.back(); }
        RelationEdge *operator->() { return &stack.back(); }
        const RelationEdge &operator*() const { return stack.back(); }
        const RelationEdge *operator->() const { return &stack.back(); }
    };

    EdgeIterator begin(BucketSet &visited, const RelationBits &relations,
                       bool undirectedOnly) {
        return EdgeIterator(*this, visited, relations, undirectedOnly);
    }

    EdgeIterator begin(BucketSet &visited) {
        return begin(visited, allRelations, true);
    }

    EdgeIterator end(BucketSet &visited) { return EdgeIterator(visited); }

    /*********************** end iterator stuff **********************/

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out, const Bucket &bucket) {
        for (auto &pair : bucket.relatedBuckets) {
            for (auto &related : pair.second)
                out << "{ " << related.get().id << " } " << pair.first << " { "
                    << bucket.id << " }";
        }
        return out;
    }
#endif
};

class RelationsGraph {
    using UniqueBucketSet = std::set<std::unique_ptr<Bucket>>;

    class EdgeIterator {
        using BucketIterator = UniqueBucketSet::iterator;
        Bucket::BucketSet visited;

        RelationBits allowedEdges;
        bool undirectedOnly;

        BucketIterator bucketIt;
        BucketIterator endIt;
        Bucket::EdgeIterator edgeIt;

        void nextViableEdge() {
            while (edgeIt == (*bucketIt)->end(visited)) {
                ++bucketIt;
                if (bucketIt == endIt)
                    return;
                edgeIt = (*bucketIt)->begin(visited, allowedEdges,
                                            undirectedOnly);
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
                     const RelationBits &a, bool u)
                : allowedEdges(a), undirectedOnly(u), bucketIt(start),
                  endIt(end), edgeIt((*bucketIt)->begin(visited, allowedEdges,
                                                        undirectedOnly)) {
            assert(bucketIt != endIt && "at least one bucket");
            nextViableEdge();
        }

        EdgeIterator(const EdgeIterator &other)
                : visited(other.visited), allowedEdges(other.allowedEdges),
                  undirectedOnly(other.undirectedOnly),
                  bucketIt(other.bucketIt), endIt(other.endIt),
                  edgeIt(other.edgeIt) {
            edgeIt.setVisited(visited);
        }

        EdgeIterator(EdgeIterator &&other)
                : visited(std::move(other.visited)),
                  allowedEdges(std::move(other.allowedEdges)),
                  undirectedOnly(std::move(undirectedOnly)),
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
            swap(lt.allowedEdges, rt.allowedEdges);
            swap(lt.undirectedOnly, rt.undirectedOnly);
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
    size_t nextId = 0;

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
    using iterator = EdgeIterator;

    RelationBits relationsBetween(Bucket &lt, Bucket &rt) const {
        RelationsMap result = getRelated(rt, allRelations);
        return result[lt];
    }

    using RelationsMap = std::map<std::reference_wrapper<Bucket>, RelationBits>;

    iterator begin_related(Bucket &start, const RelationBits &relations,
                           bool undirectedOnly = false) const {
        auto startIt = getItFor(start);
        auto endIt = startIt;
        return iterator(startIt, ++endIt, relations, undirectedOnly);
    }

    iterator begin_related(Bucket &start) const {
        return begin_related(start, allRelations, true);
    }

    iterator end_related(Bucket &start) const {
        auto endIt = ++getItFor(start);
        return iterator(endIt);
    }

    iterator begin() const { return begin(allRelations, true); }

    iterator begin(const RelationBits &relations,
                   bool undirectedOnly = false) const {
        if (!buckets.empty())
            return iterator(buckets.begin(), buckets.end(), relations,
                            undirectedOnly);
        return end();
    }

    iterator end() const { return iterator(buckets.end()); }

    RelationsMap getRelated(Bucket &start, const RelationBits &relations,
                            bool toFirstStrict = false) const;

    RelationsMap getRelated(Bucket &start, const RelationBits &relations,
                            bool toFirstStrict = false) const;

    bool areRelated(Bucket &lt, RelationType type, Bucket &rt,
                    RelationBits *maybeBetween = nullptr) const;

    bool haveConflictingRelation(Bucket &lt, RelationType type, Bucket &rt,
                                 RelationBits *maybeBetween = nullptr) const;

    Bucket &getNewBucket() {
        auto pair = buckets.emplace(new Bucket(++nextId));
        return **pair.first;
    }

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
