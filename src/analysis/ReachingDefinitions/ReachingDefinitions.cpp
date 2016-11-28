#include <set>

#include "RDMap.h"
#include "ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {

RDNode UNKNOWN_MEMLOC;
RDNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

bool ReachingDefinitionsAnalysis::processNode(RDNode *node)
{
    bool changed = false;

    // merge maps from predecessors
    for (RDNode *n : node->predecessors)
        changed |= node->def_map.merge(&n->def_map,
                                       &node->overwrites /* strong update */,
                                       strong_update_unknown,
                                       max_set_size /* max size of set of reaching definition
                                                       of one definition site */,
                                       false /* merge unknown */);

    return changed;
}

} // namespace rd
} // namespace analysis
} // namespace dg
