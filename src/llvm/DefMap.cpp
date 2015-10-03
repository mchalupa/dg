#include <algorithm>
#include "DefMap.h"

namespace dg {

class LLVMNode;

namespace analysis {


DefMap::DefMap(const DefMap& o)
{
    merge(&o);
}

bool DefMap::merge(const DefMap *oth, PointsToSetT *without)
{
    bool changed = false;

    if (this == oth)
        return false;

    for (auto it : oth->defs) {
        const Pointer& ptr = it.first;

        // should we skip this pointer
        if (without && without->count(ptr) != 0)
            continue;

        // our values that we have for
        // this pointer
        ValuesSetT& our_vals = defs[ptr];

        // copy values that have map oth for the
        // pointer to our values
        for (LLVMNode *defnode : it.second) {
            changed |= our_vals.insert(defnode).second;
        }
    }

    return changed;
}

bool DefMap::add(const Pointer& p, LLVMNode *n)
{
    return defs[p].insert(n).second;
}

bool DefMap::update(const Pointer& p, LLVMNode *n)
{
    bool ret;
    ValuesSetT& dfs = defs[p];

    ret = dfs.count(n) == 0 || dfs.size() > 1;
    dfs.clear();
    dfs.insert(n);

    return ret;
}

bool DefMap::definesWithAnyOffset(const Pointer& p)
{
    // FIXME do it via binary search
    for (auto it : defs)
        if (it.first.obj == p.obj)
            return true;

    return false;
}

static bool comp(const std::pair<const Pointer, ValuesSetT>& a,
                 const std::pair<const Pointer, ValuesSetT>& b)
{
    return a.first.obj < b.first.obj;
}

std::pair<DefMap::iterator, DefMap::iterator> DefMap::getObjectRange(const Pointer& ptr)
{
    std::pair<const Pointer, ValuesSetT> what(ptr, ValuesSetT());
    return std::equal_range(defs.begin(), defs.end(), what, comp);
}

} // analysis
} // dg
