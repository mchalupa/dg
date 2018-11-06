#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;

static bool comp_ds(const DefSite& a, const DefSite& b)
{
    return a.target < b.target;
}

///
// merge @oth map to this map. If given @no_update set,
// take those definitions as 'overwrites'. That is -
// if some definition in @no_update set overwrites definition
// in @oth set, don't merge it to our map. The exception are
// definitions with Offset::UNKNOWN, since we don't know what
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
bool BasicRDMap::merge(const BasicRDMap *oth,
                       DefSiteSetT *no_update,
                       bool strong_update_unknown,
                       Offset::type max_set_size,
                       bool merge_unknown)
{
    if (this == oth)
        return false;

    bool changed = false;
    for (const auto& it : oth->_defs) {
        const DefSite& ds = it.first;
        bool is_unknown = ds.offset.isUnknown();

        // STRONG UPDATE
        // --------------------
        // should we update this def-site (strong update)?
        // but only if the offset is concrete, because if
        // it is not concrete, we want to do weak update
        // Also, we don't want to do strong updates for
        // heap allocated objects, since they are all represented
        // by the call site
        if (no_update) { // do we have anything for strong update at all?
            // if the memory is defined at unknown offset, we can
            // still do a strong update provided this is the update
            // of whole memory (so we need to know the size of the memory).
            if (strong_update_unknown &&
                is_unknown && ds.target->getSize() > 0) {
                // get the writes that should overwrite this definition
                auto range = std::equal_range(no_update->begin(),
                                              no_update->end(),
                                              ds, comp_ds);
                // XXX: we could check wether all the strong updates
                // together overwrite the memory, but that could be
                // to much work. Just check wether there's is just a one
                // update that overwrites the whole memory
                bool overwrites_whole_memory = false;
                for (auto I = range.first; I!= range.second; ++I) {
                    const DefSite& ds2 = *I;
                    assert(ds.target == ds2.target);
                    if (*ds2.offset == 0 && *ds2.len >= ds.target->getSize()) {
                        overwrites_whole_memory = true;
                        break;
                    }
                }

                // do strong update - just continue the loop without
                // merging this definition into our map
                if (overwrites_whole_memory)
                    continue;
            } else if (ds.target->getType() != RDNodeType::DYN_ALLOC) {
                bool skip = false;
                auto range = std::equal_range(no_update->begin(),
                                              no_update->end(),
                                              ds, comp_ds);
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
        }

        // MERGE CONCRETE OFFSETS (if desired)
        // ------------------------------------
        RDNodesSet *our_vals = nullptr;
        if (merge_unknown && is_unknown) {
            // this loop finds all concrete offsets and merges them into one
            // defsite with Offset::UNKNOWN. This defsite is set in 'our_vals'
            // after the loop exit
            our_vals = &_defs[DefSite(ds.target, Offset::UNKNOWN, Offset::UNKNOWN)];

            auto range = getObjectRange(ds);
            for (auto I = range.first; I != range.second;) {
                auto cur = I++;

                // this must hold (getObjectRange)
                assert(cur->first.target == ds.target);

                // don't remove the one with Offset::UNKNOWN
                if (&cur->second == our_vals)
                    continue;

                // merge values with concrete offset to
                // this unknown offset
                for (RDNode *defnode : cur->second)
                    changed |= our_vals->insert(defnode);

                // erase the def-site with concrete offset
                _defs.erase(cur);
            }

            // fall-through to add the new definitions from the other map
        } else {
            // our values that we have for this definition-site
            our_vals = &_defs[ds];
        }

        assert(our_vals && "BUG");

        // copy values that have the map 'oth' for the defsite 'ds' to our map
        for (RDNode *defnode : it.second)
            changed |= our_vals->insert(defnode);

        // crop the set to UNKNOWN_MEMORY if it is too big.
        // But only in the case that the  DefSite is not also UNKNOWN,
        // because then we would be 'unknown memory defined @ unknown place'
        if (!ds.target->isUnknown() && our_vals->size() > max_set_size)
            our_vals->makeUnknown();
    }

    return changed;
}

bool BasicRDMap::add(const DefSite& p, RDNode *n)
{
    return _defs[p].insert(n);
}

bool BasicRDMap::update(const DefSite& p, RDNode *n)
{
    bool ret;
    RDNodesSet& dfs = _defs[p];

    ret = dfs.count(n) == 0 || dfs.size() > 1;
    dfs.clear();
    dfs.insert(n);

    return ret;
}

size_t BasicRDMap::get(RDNode *n, const Offset& off,
                       const Offset& len, std::set<RDNode *>& ret)
{
    DefSite ds(n, off, len);
    return get(ds, ret);
}

size_t BasicRDMap::get(DefSite& ds, std::set<RDNode *>& ret)
{
    if (ds.offset.isUnknown()) {
        auto range = getObjectRange(ds);
        for (auto I = range.first; I != range.second; ++I) {
            assert(I->first.target == ds.target);
            ret.insert(I->second.begin(), I->second.end());
        }
    } else {
        auto range = getObjectRange(ds);
        for (auto I = range.first; I != range.second; ++I) {
            assert(I->first.target == ds.target);
            // if we found a definition with Offset::UNKNOWN,
            // it is possibly a definition that we need */
            if (I->first.offset.isUnknown() ||
                intervalsOverlap(*I->first.offset, *I->first.len,
                                *ds.offset, *ds.len)){
            ret.insert(I->second.begin(), I->second.end());
            }
        }
    }

    return ret.size();
}


static inline bool comp(const std::pair<const DefSite, RDNodesSet>& a,
                        const std::pair<const DefSite, RDNodesSet>& b)
{
    return a.first.target < b.first.target;
}

std::pair<BasicRDMap::MapT::iterator, BasicRDMap::MapT::iterator>
BasicRDMap::getObjectRange(const DefSite& ds)
{
    std::pair<const DefSite, RDNodesSet> what(ds, RDNodesSet());
    return std::equal_range(_defs.begin(), _defs.end(), what, comp);
}

std::pair<BasicRDMap::MapT::const_iterator, BasicRDMap::MapT::const_iterator>
BasicRDMap::getObjectRange(const DefSite& ds) const
{
    std::pair<const DefSite, RDNodesSet> what(ds, RDNodesSet());
    return std::equal_range(_defs.begin(), _defs.end(), what, comp);
}

} // rd
} // analysis
} // dg
