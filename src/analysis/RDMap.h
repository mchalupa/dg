#ifndef _DG_DEF_MAP_H_
#define _DG_DEF_MAP_H_

#include <set>
#include <map>
#include <cassert>

#include "Offset.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

static bool
intervalsDisjunctive(uint64_t a1, uint64_t a2,
                     uint64_t b1, uint64_t b2)
{
    // XXX: does this work if a2 or b2 is UNKNOWN_OFFSET??
    // think it through
    return ((a1 <= b1) ? (a2 < b1) : (b2 < a1));
}

static bool
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
        assert(o.isUnknown() || l.isUnknown() ||
               *o + *l > 0 && "Invalid offset and length given");
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

typedef std::set<DefSite> DefSiteSetT;
typedef std::set<RDNode *> RDNodesSetT;

class RDMap
{
    std::map<DefSite, RDNodesSetT> defs;

public:
    typedef std::map<DefSite, RDNodesSetT>::iterator iterator;
    typedef std::map<DefSite, RDNodesSetT>::const_iterator const_iterator;

    RDMap() {}
    RDMap(const RDMap& o);

    bool merge(const RDMap *o,
               DefSiteSetT *without = nullptr,
               bool merge_unknown = false);
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

    RDNodesSetT& get(const DefSite& ds){ return defs[ds]; }
    //const RDNodesSetT& get(const DefSite& ds) const { return defs[ds]; }
    RDNodesSetT& operator[](const DefSite& ds) { return defs[ds]; }
    //RDNodesSetT& get(RDNode *, const Offset&);
    // gather reaching definitions of memory [n + off, n + off + len]
    // and store them to the @ret
    size_t get(RDNode *n, const Offset& off,
               const Offset& len, std::set<RDNode *>& ret)
    {
        if (off.isUnknown()) {
            // FIXME: use getObjectRange()
            for (auto it : defs)
                if (it.first.target == n)
                    ret.insert(it.second.begin(), it.second.end());
        } else {
            // FIXME: use getObjectRange()
            for (auto it : defs)
                if (it.first.target == n
                       /* if we found a definition with UNKNOWN_OFFSET,
                        * it is possibly a definition that we need */
                    && (it.first.offset.isUnknown() ||
                        /* if the length is unknown, then just check
                         * if the starts can overlap */
                        (len.isUnknown() && *off <= *it.first.offset) ||
                        /* just check if the offsets + length have
                         * some overlap */
                        intervalsOverlap(*it.first.offset,
                                        /* -1 because we're starting from 0 */
                                        *it.first.offset + *it.first.len - 1,
                                        *off, *off + *len - 1))){
                    ret.insert(it.second.begin(), it.second.end());
                }
        }

        return ret.size();
    }

    const std::map<DefSite, RDNodesSetT> getDefs() const { return defs; }
};

} // rd
} // analysis
} // dg

#endif
