#include <set>

#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/BBlocksBuilder.h"

namespace dg {
namespace analysis {
namespace rd {

RDNode UNKNOWN_MEMLOC;
RDNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;


void ReachingDefinitionsGraph::buildBBlocks() {
    assert(getRoot() && "No root node");

    BBlocksBuilder<RDBBlock> builder;
    _bblocks = std::move(builder.buildAndGetBlocks(getRoot()));
}


bool ReachingDefinitionsAnalysis::processNode(RDNode *node)
{
    bool changed = false;

    // merge maps from predecessors
    for (RDNode *n : node->predecessors)
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
    assert(getRoot() && "Do not have root");

    std::vector<RDNode *> to_process = getNodes(getRoot());
    std::vector<RDNode *> changed;

    // do fixpoint
    do {
        unsigned last_processed_num = to_process.size();
        changed.clear();

        for (RDNode *cur : to_process) {
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
}

std::vector<RDNode *>
SSAReachingDefinitionsAnalysis::readValue(RDBBlock *block,
                                          RDNode *node,
                                          const DefSite& ds,
                                          bool addDefuse) {
    // add def-use edges to known definitions
    if (addDefuse) {
        auto defs = block->definitions.get(ds);
        node->defuse = decltype(node->defuse)(defs.begin(), defs.end());
    }

    std::vector<RDNode *> phis;
    // find out which bytes are not covered yet
    // and add phi nodes for these intervals
    auto uncovered = block->definitions.undefinedIntervals(ds);
    for (auto& interval : uncovered) {
        phis.emplace_back(graph.create(RDNodeType::PHI));
        phis.back()->addOverwrites(ds.target,
                                   interval.start,
                                   interval.length());
        if (addDefuse)
            node->defuse.push_back(phis.back());
    }

    return phis;
}


void SSAReachingDefinitionsAnalysis::performLvn() {
    for (RDBBlock *block : graph.blocks()) {
        // new phi nodes
        std::vector<RDNode *> phis;

        // perform Lvn for one block
        for (RDNode *node : block->getNodes()) {
            // strong update
            for (auto& ds : node->overwrites) {
                block->definitions.update(ds, node);
            }

            // weak update
            for (auto& ds : node->defs) {
                // since this is just weak update,
                // look for the previous definitions of 'ds'
                // and if there are none, add a PHI node
                auto newphis = readValue(block, node, ds,
                                         false /* do not add def-def edges */);
                phis.insert(phis.end(), newphis.begin(), newphis.end());

                block->definitions.add(ds, node);
                block->definitions.add(ds, newphis);
            }

            // is this node a use? If so then update def-use edges
            for (auto& ds : node->uses) {
                auto newphis = readValue(block, node, ds);
                phis.insert(phis.end(), newphis.begin(), newphis.end());

                // we added new phi nodes and those act as definitions,
                // so register new definitions
                block->definitions.add(ds, newphis);
            }
        }

        // add all the new phi nodes to the current block
        for (auto phi : phis) {
            block->prependAndUpdateCFG(phi);
        }
    }
}

void SSAReachingDefinitionsAnalysis::performGvn() {
}

} // namespace rd
} // namespace analysis
} // namespace dg
