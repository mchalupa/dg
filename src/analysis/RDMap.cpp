#include <algorithm>
#include "RDMap.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;

RDMap::RDMap(const RDMap& o)
{
    merge(&o);
}

bool RDMap::merge(const RDMap *oth, DefSiteSetT *without)
{
    if (this == oth)
        return false;

    bool changed = false;
    for (auto it : oth->defs) {
        const DefSite& ds = it.first;

        // FIXME: unknown offsets

        // should we update this def-site (strong update)?
        if (without) {
            bool skip = false;
            // FIXME: use getObjectRange
            for (const DefSite& ds2 : *without) {
                if (ds.target != ds2.target)
                    continue;

                // targets are the same
                if ((*ds.offset >= *ds2.offset)
                    && (*ds.offset + *ds.len <= *ds2.offset + *ds2.len)) {
                    skip = true;
                    break;
                }
            }

            if (skip)
                continue;
        }

        // our values that we have for
        // this pointer
        RDNodesSetT& our_vals = defs[ds];

        // copy values that have map oth for the
        // pointer to our values
        for (RDNode *defnode : it.second) {
            changed |= our_vals.insert(defnode).second;
        }
    }

    return changed;
}

bool RDMap::add(const DefSite& p, RDNode *n)
{
    return defs[p].insert(n).second;
}

bool RDMap::update(const DefSite& p, RDNode *n)
{
    bool ret;
    RDNodesSetT& dfs = defs[p];

    ret = dfs.count(n) == 0 || dfs.size() > 1;
    dfs.clear();
    dfs.insert(n);

    return ret;
}

bool RDMap::definesWithAnyOffset(const DefSite& ds)
{
    // FIXME do it via binary search
    for (auto it : defs)
        if (it.first.target == ds.target)
            return true;

    return false;
}

static bool comp(const std::pair<const DefSite, RDNodesSetT>& a,
                 const std::pair<const DefSite, RDNodesSetT>& b)
{
    return a.first.target < b.first.target;
}

std::pair<RDMap::iterator, RDMap::iterator>
getObjectRange(RDNode *n)
{
    abort();
}

std::pair<RDMap::iterator, RDMap::iterator>
RDMap::getObjectRange(const DefSite& ds)
{
    std::pair<const DefSite, RDNodesSetT> what(ds, RDNodesSetT());
    return std::equal_range(defs.begin(), defs.end(), what, comp);
}

} // rd
} // analysis
} // dg
