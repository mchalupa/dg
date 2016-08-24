#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "RDMap.h"
#include "ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;

RDMap::RDMap(const RDMap& o)
{
    merge(&o);
}

///
// merge @oth map to this map. If given @no_update set,
// take those definitions as 'overwrites'. That is -
// if some definition in @no_update set overwrites definition
// in @oth set, don't merge it to our map. The exception are
// definitions with UNKNOWN_OFFSET, since we don't know what
// places can these overwrite, these are always added (weak update).
// If @merge_unknown flag is set to true, all definitions
// with concrete offset are merge to the definition with UNKNOWN offset
// once this definition is found (this is because to a def-use
// relation the concrete OFFSET and UNKNOWN offset act the same, that is:
//
//   def(A, 0, 4) at NODE1
//   def(A, UNKNOWN) at NODE2
//   use(A, 2)
//
// The use has reaching definitions NODE1 and NODE2, thus we can
// just have that merged to UNKNOWN, since UNKNOWN may be 0-4:
//
//   def(A, UNKNOWN) at NODE1, NODE2
//   use(A, 2)
//
// That may introduce some unprecision, though:
//
//   def(A, 0, 4) at NODE1
//   def(A, 4, 8) at NODE3
//   def(A, UNKNOWN) at NODE2
//   use(A, 2) -- reaching is just NODE1 and NODE2
//   ---
//   def(A, UNKNOWN) at NODE1, NODE2, NODE2
//                      -- reaching are all thre
//
// This is useful when we have a lot of concrete and unknown definitions
// in the map
bool RDMap::merge(const RDMap *oth,
                  DefSiteSetT *no_update,
                  bool merge_unknown)
{
    if (this == oth)
        return false;

    bool changed = false;
    for (auto it : oth->defs) {
        const DefSite& ds = it.first;
        bool is_unknown = ds.offset.isUnknown();

        // should we update this def-site (strong update)?
        // but only if the offset is concrete, because if
        // it is not concrete, we want to do weak update
        // Also, we don't want to do strong updates for
        // heap allocated objects, since they are all represented
        // by the call site
        if (!is_unknown && no_update
            && ds.target->getType() != DYN_ALLOC) {
            bool skip = false;

            auto range = std::equal_range(no_update->begin(),
                                          no_update->end(),
                                          ds,[](const DefSite& a,
                                                const DefSite& b) -> bool
                                                { return a.target < b.target; });
            for (auto I = range.first; I!= range.second; ++I) {
                const DefSite& ds2 = *I;
                assert(ds.target == ds2.target);
                // if the 'no_update' set contains target with unknown
                // pointer, we should always keep that value
                // and the value being merged (just all possible definitions)
                if (ds2.offset.isUnknown()) {
                    // break no_update skip = true, thus adding
                    // the values for UNKOWN to our map
                    is_unknown = true;
                    break;
                }

                // targets are the same, check if the what we have
                // in 'no_update' set overwrites the values that are in
                // the other map
                if ((*ds.offset >= *ds2.offset)
                    && (*ds.offset + *ds.len <= *ds2.offset + *ds2.len)) {
                    skip = true;
                    break;
                }
            }

            // if values in 'no_update' map overwrite the coresponding values
            // in the other map, don't update our map
            if (skip)
                continue;
        }

        RDNodesSet *our_vals = nullptr;
        if (merge_unknown && is_unknown) {
            // merge all concrete offset to UNKOWN
            our_vals = &defs[DefSite(ds.target, UNKNOWN_OFFSET, UNKNOWN_OFFSET)];

            for (auto I = defs.begin(), E = defs.end(); I != E;) {
                auto cur = I++;
                // look only for our target
                if (cur->first.target != ds.target)
                    continue;

                // don't remove the one with UNKNOWN_OFFSET
                if (&cur->second == our_vals)
                    continue;

                // merge values with concrete offset to
                // this unknown offset
                for (RDNode *defnode : cur->second)
                    changed |= our_vals->insert(defnode);

                // erase the def-site with concrete offset
                defs.erase(cur);
            }

            // fall-through to add the new definitions from the other map
        } else {
            // our values that we have for this definition-site
            our_vals = &defs[ds];
        }

        assert(our_vals && "BUG");

        // copy values that have the map 'oth' for the defsite 'ds' to our map
        for (RDNode *defnode : it.second)
            changed |= our_vals->insert(defnode);
    }

    return changed;
}

bool RDMap::add(const DefSite& p, RDNode *n)
{
    return defs[p].insert(n);
}

bool RDMap::update(const DefSite& p, RDNode *n)
{
    bool ret;
    RDNodesSet& dfs = defs[p];

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

static inline bool comp(const std::pair<const DefSite, RDNodesSet>& a,
                        const std::pair<const DefSite, RDNodesSet>& b)
{
    return a.first.target < b.first.target;
}

std::pair<RDMap::iterator, RDMap::iterator>
RDMap::getObjectRange(const DefSite& ds)
{
    std::pair<const DefSite, RDNodesSet> what(ds, RDNodesSet());
    return std::equal_range(defs.begin(), defs.end(), what, comp);
}

} // rd
} // analysis
} // dg
