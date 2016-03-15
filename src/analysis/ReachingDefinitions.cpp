#include <set>

#include "RDMap.h"
#include "ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {

bool ReachingDefinitionsAnalysis::processNode(RDNode *node)
{
    bool changed = false;

    // merge maps from predecessors
    for (RDNode *n : node->predecessors)
        changed |= node->def_map.merge(&n->def_map,
                                       &node->overwrites /* strong update */);

    return changed;
}

} // namespace rd
} // namespace analysis
} // namespace dg
