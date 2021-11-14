#ifndef DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_
#define DG_LLVM_VALUE_RELATIONS_RELATION_BUCKETS_H_

#ifndef NDEBUG
#include <iostream>
#endif

#include <algorithm>
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

struct Relations {
    enum Type { EQ, NE, LE, LT, GE, GT, PT, PF };
    static const size_t total = 8;

    static Type inverted(Type type);
    static Type negated(Type type);
    static bool transitiveOver(Type one, Type two);
    static Relations conflicting(Type type);

    bool has(Type type) const { return bits[type]; }
    Relations &set(Type t) {
        bits.set(t);
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
    Relations &invert() {
        ~bits;
        return *this;
    }
    bool conflictsWith(Type type) const {
        return (bits & conflicting(type).bits).any();
    }
    bool anyCommon(const Relations &other) const {
        return (bits & other.bits).any();
    }

  private:
    std::bitset<total> bits;
};

RelationType toRelation(size_t i);
size_t toInt(RelationType t);
RelationType inverted(RelationType type);
RelationType negated(RelationType type);
RelationBits conflicting(RelationType type);
bool transitiveOver(RelationType fst, RelationType snd);

extern const RelationBits allRelations;

#ifndef NDEBUG
std::ostream &operator<<(std::ostream &out, RelationType r);
std::ostream &operator<<(std::ostream &out, Relations::Type r);
#endif

class Bucket {
  public:
    using BucketSet = std::set<std::reference_wrapper<Bucket>>;

  private:
    // R -> { a } such that (this, a) \in R (e.g. LE -> { a } such that this LE
    // a)
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
                if (related != *this)
                    setRelated(*this, pair.first, related);
            }
        }
    }

    void disconnect() {
        for (auto &pair : relatedBuckets) {
            for (auto it = pair.second.begin(); it != pair.second.end();
                 /*incremented by erase*/) {
                it->get().relatedBuckets[inverted(pair.first)].erase(*this);
                it = relatedBuckets[pair.first].erase(it);
            }
        }
        assert(!hasAnyRelation());
    }

    friend void setRelated(Bucket &lt, RelationType type, Bucket &rt) {
        assert(lt != rt && "no reflexive relations");
        lt.relatedBuckets[type].emplace(rt);
        rt.relatedBuckets[inverted(type)].emplace(lt);
    }

    friend void unsetRelated(Bucket &lt, RelationType type, Bucket &rt) {
        lt.relatedBuckets[type].erase(rt);
        rt.relatedBuckets[inverted(type)].erase(lt);
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
            out << nEdge.from().id << " " << edge.rel() << " " << nEdge.to().id;
            return out;
        }
#endif
    };

    class EdgeIterator {
        std::vector<RelationEdge> stack;
        std::reference_wrapper<BucketSet> visited;

      public:
        RelationBits allowedEdges;
        bool undirectedOnly;
        bool relationsFocused;

      private:
        bool isInvertedEdge() {
            return stack.size() >= 2 &&
                   std::any_of(
                           std::next(stack.rbegin()), stack.rend(),
                           [&topTo = stack.back().to()](RelationEdge &edge) {
                               return edge.from() == topTo;
                           });
        }

        bool shouldFollowThrough() {
            if (stack.size() < 2)
                return true;
            RelationType prev = std::next(stack.rbegin())->rel();
            return transitiveOver(prev, stack.back().rel());
        }

        bool nextViableTopEdge() {
            while (stack.back().nextViableEdge()) {
                if (allowedEdges[toInt(stack.back().rel())] &&
                    (!undirectedOnly || !isInvertedEdge()) &&
                    (!relationsFocused || shouldFollowThrough()))
                    return true;
                ++stack.back().bucketIt;
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
        EdgeIterator(BucketSet &v) : visited(v) {}
        // for begin iterator
        EdgeIterator(Bucket &start, BucketSet &v, const RelationBits &a, bool u,
                     bool r)
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
            RelationEdge current = stack.back();
            stack.pop_back();

            Bucket &to = current.to();

            // plan return to next successor of "from" bucket
            ++current.bucketIt;
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
                       bool undirectedOnly, bool relationsFocused) {
        return EdgeIterator(*this, visited, relations, undirectedOnly,
                            relationsFocused);
    }

    EdgeIterator begin(BucketSet &visited) {
        return begin(visited, allRelations, true, true);
    }

    EdgeIterator end(BucketSet &visited) { return EdgeIterator(visited); }

    /*********************** end iterator stuff **********************/

#ifndef NDEBUG
    friend std::ostream &operator<<(std::ostream &out, const Bucket &bucket) {
        for (auto &pair : bucket.relatedBuckets) {
            for (auto &related : pair.second)
                out << "{ " << bucket.id << " } " << pair.first << " { "
                    << related.get().id << " }";
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

        BucketIterator bucketIt;
        BucketIterator endIt;
        Bucket::EdgeIterator edgeIt;

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
                     const RelationBits &a, bool u, bool r)
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
        RelationsMap result = getRelated(lt, allRelations);
        return result[rt];
    }

    using RelationsMap = std::map<std::reference_wrapper<Bucket>, RelationBits>;

    iterator begin_related(Bucket &start, const RelationBits &relations,
                           bool undirectedOnly = true) const {
        auto startIt = getItFor(start);
        auto endIt = startIt;
        return iterator(startIt, ++endIt, relations, undirectedOnly, true);
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
                   bool undirectedOnly = true) const {
        if (!buckets.empty())
            return iterator(buckets.begin(), buckets.end(), relations,
                            undirectedOnly, false);
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
