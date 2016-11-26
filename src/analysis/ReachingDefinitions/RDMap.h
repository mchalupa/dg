#ifndef _DG_DEF_MAP_H_
#define _DG_DEF_MAP_H_

#include <set>
#include <map>
#include <cassert>

#include "analysis/Offset.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

inline bool
intervalsDisjunctive(uint64_t a1, uint64_t a2,
                     uint64_t b1, uint64_t b2)
{
    // XXX: does this work if a2 or b2 is UNKNOWN_OFFSET??
    // think it through
    return ((a1 <= b1) ? (a2 < b1) : (b2 < a1));
}

inline bool
intervalsOverlap(uint64_t a1, uint64_t a2,
                 uint64_t b1, uint64_t b2)
{
    return !intervalsDisjunctive(a1, a2, b1, b2);
}

struct DefSite
{
    DefSite(RDNode *t,
            const Offset& o = UNKNOWN_OFFSET,
            const Offset& l = UNKNOWN_OFFSET)
        : target(t), offset(o), len(l)
    {
        assert((o.isUnknown() || l.isUnknown() ||
               *o + *l > 0) && "Invalid offset and length given");
    }

    bool operator<(const DefSite& oth) const
    {
        return target == oth.target ?
                (offset == oth.offset ? len < oth.len : offset < oth.offset)
                : target < oth.target;
    }

    // what memory this node defines
    RDNode *target;
    // on what offset
    Offset offset;
    // how many bytes
    Offset len;
};

extern RDNode *UNKNOWN_MEMORY;

// wrapper around std::set<> with few
// improvements that will be handy in our set-up
class RDNodesSet {
    std::set<RDNode *> nodes;
    bool is_unknown;

public:
    RDNodesSet() : is_unknown(false) {}

    // the set contains unknown mem. location
    void makeUnknown()
    {
        nodes.clear();
        nodes.insert(UNKNOWN_MEMORY);
        is_unknown = true;
    }

    bool insert(RDNode *n)
    {
        if (is_unknown)
            return false;

        if (n == UNKNOWN_MEMORY) {
            makeUnknown();
            return true;
        } else
            return nodes.insert(n).second;
    }

    size_t count(RDNode *n) const
    {
        return nodes.count(n);
    }

    size_t size() const
    {
        return nodes.size();
    }

    void clear()
    {
        nodes.clear();
        is_unknown = false;
    }

    bool isUnknown() const
    {
        return is_unknown;
    }

    std::set<RDNode *>::iterator begin() { return nodes.begin(); }
    std::set<RDNode *>::iterator end() { return nodes.end(); }
    std::set<RDNode *>::const_iterator begin() const { return nodes.begin(); }
    std::set<RDNode *>::const_iterator end() const { return nodes.end(); }

    std::set<RDNode *>& getNodes()
    {
        return nodes;
    };

};

typedef std::set<DefSite> DefSiteSetT;

class RDMap
{
public:
    typedef std::map<DefSite, RDNodesSet> MapT;
    typedef MapT::iterator iterator;
    typedef MapT::const_iterator const_iterator;

    RDMap() {}
    RDMap(const RDMap& o);

    bool merge(const RDMap *o,
               DefSiteSetT *without = nullptr,
               bool strong_update_unknown = true,
               uint32_t max_set_size  = (~((uint32_t) 0)),
               bool merge_unknown     = false);
    bool add(const DefSite&, RDNode *n);
    bool update(const DefSite&, RDNode *n);
    bool empty() const { return defs.empty(); }

    // @return iterators for the range of pointers that has the same object
    // as the given def site
    std::pair<RDMap::iterator, RDMap::iterator>
    getObjectRange(const DefSite&);

    std::pair<RDMap::iterator, RDMap::iterator>
    getObjectRange(RDNode *);

    bool defines(const DefSite& ds) { return defs.count(ds) != 0; }
    bool definesWithAnyOffset(const DefSite& ds);

    iterator begin() { return defs.begin(); }
    iterator end() { return defs.end(); }
    const_iterator begin() const { return defs.begin(); }
    const_iterator end() const { return defs.end(); }

    RDNodesSet& get(const DefSite& ds){ return defs[ds]; }
    //const RDNodesSetT& get(const DefSite& ds) const { return defs[ds]; }
    RDNodesSet& operator[](const DefSite& ds) { return defs[ds]; }

    //RDNodesSet& get(RDNode *, const Offset&);
    // gather reaching definitions of memory [n + off, n + off + len]
    // and store them to the @ret
    size_t get(RDNode *n, const Offset& off,
               const Offset& len, std::set<RDNode *>& ret);
    size_t get(DefSite& ds, std::set<RDNode *>& ret);

    const MapT& getDefs() const { return defs; }

private:
     MapT defs;
};

} // rd
} // analysis
} // dg

#endif
