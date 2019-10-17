#include <set>
#include <vector>

#ifndef NDEBUG
#include <iostream>
#endif

#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {

RWNode UNKNOWN_MEMLOC;
RWNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

#ifndef NDEBUG
void RWNode::dump() const {
	std::cout << getID() << "\n";
}
#endif

bool ReachingDefinitionsAnalysis::processNode(RWNode *node)
{
    bool changed = false;

    // merge maps from predecessors
    for (RWNode *n : node->getPredecessors())
        changed |= node->def_map.merge(&n->def_map,
                                       &node->overwrites /* strong update */,
                                       options.strongUpdateUnknown,
                                       *options.maxSetSize, /* max size of set of reaching definition
                                                              of one definition site */
                                       false /* merge unknown */);

    return changed;
}

void ReachingDefinitionsAnalysis::run()
{
    DBG_SECTION_BEGIN(dda, "Starting reaching definitions analysis");
    assert(getRoot() && "Do not have root");

    std::vector<RWNode *> to_process = getNodes(getRoot());
    std::vector<RWNode *> changed;

#ifdef DEBUG_ENABLED
    int n = 0;
#endif

    // do fixpoint
    do {
#ifdef DEBUG_ENABLED
        if (n % 100 == 0) {
            DBG(dda, "Iteration " << n << ", queued " << to_process.size() << " nodes");
        }
        ++n;
#endif
        unsigned last_processed_num = to_process.size();
        changed.clear();

        for (RWNode *cur : to_process) {
            if (processNode(cur))
                changed.push_back(cur);
        }

        if (!changed.empty()) {
            to_process.clear();
            to_process = getNodes(changed /* starting set */,
                                  last_processed_num /* expected num */);

            // since changed was not empty,
            // the to_process must not be empty too
            assert(!to_process.empty());
        }
    } while (!changed.empty());

    DBG_SECTION_END(dda, "Finished reaching definitions analysis");
}

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RWNode *>
ReachingDefinitionsAnalysis::getReachingDefinitions(RWNode *where, RWNode *mem,
                                                    const Offset& off,
                                                    const Offset& len)
{
    std::set<RWNode *> ret;
    if (mem->isUnknown()) {
        // gather all definitions of memory
        for (auto& it : where->def_map) {
            ret.insert(it.second.begin(), it.second.end());
        }
    } else {
        // gather all possible definitions of the memory
        where->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);
        where->def_map.get(mem, off, len, ret);
    }

    return std::vector<RWNode *>(ret.begin(), ret.end());
}

std::vector<RWNode *>
ReachingDefinitionsAnalysis::getReachingDefinitions(RWNode *use) {
    std::set<RWNode *> ret;

    // gather all possible definitions of the memory including the unknown mem
    for (auto& ds : use->uses) {
        if (ds.target->isUnknown()) {
            // gather all definitions of memory
            for (auto& it : use->def_map) {
                ret.insert(it.second.begin(), it.second.end());
            }
            break; // we may bail out as we added everything
        }

        use->def_map.get(ds.target, ds.offset, ds.len, ret);
    }

    use->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);

    return std::vector<RWNode *>(ret.begin(), ret.end());
}

} // namespace analysis
} // namespace dg
