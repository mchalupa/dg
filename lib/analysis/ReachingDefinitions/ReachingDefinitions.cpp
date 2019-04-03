#include <set>
#include <vector>

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

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RDNode *>
ReachingDefinitionsAnalysis::getReachingDefinitions(RDNode *where, RDNode *mem,
                                                    const Offset& off,
                                                    const Offset& len)
{
    std::set<RDNode *> ret;
    // gather all possible definitions of the memory
    where->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);
    where->def_map.get(mem, off, len, ret);

    return std::vector<RDNode *>(ret.begin(), ret.end());
}

std::vector<RDNode *>
ReachingDefinitionsAnalysis::getReachingDefinitions(RDNode *use) {
    std::set<RDNode *> ret;

    // gather all possible definitions of the memory including the unknown mem
    for (auto& ds : use->uses) {
        use->def_map.get(ds.target, ds.offset, ds.len, ret);
    }

    use->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);

    return std::vector<RDNode *>(ret.begin(), ret.end());
}

std::vector<RDNode *>
SSAReachingDefinitionsAnalysis::readValue(RDBBlock *block,
                                          RDNode *node,
                                          const DefSite& ds,
                                          bool addDefuse) {
    // add def-use edges to known definitions
    if (addDefuse) {
        auto defs = block->definitions.get(ds);
        for (auto d : defs)
            node->defuse.push_back(d);
    }

    std::vector<RDNode *> phis;
    // find out which bytes are not covered yet
    // and add phi nodes for these intervals
    auto uncovered = block->definitions.undefinedIntervals(ds);
    for (auto& interval : uncovered) {
        if (auto pred = block->getSinglePredecessor()) {
            // if we have a unique predecessor, try finding definitions
            // and creating the new PHI nodes there.
            auto P = readValue(pred, node,
                               DefSite(ds.target, interval.start,
                                       interval.length()),
                               addDefuse);
            for (auto p : P)
                phis.push_back(p);
        } else {
            phis.emplace_back(graph.create(RDNodeType::PHI));
            phis.back()->addOverwrites(ds.target,
                                       interval.start,
                                       interval.length());
            if (addDefuse)
                node->defuse.push_back(phis.back());
        }
    }

    return phis;
}


std::set<RDNode *>
SSAReachingDefinitionsAnalysis::performLvn() {
    std::set<RDNode *> allphis;

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
            allphis.insert(phi);
        }
    }

    return allphis;
}

void SSAReachingDefinitionsAnalysis::performGvn(std::set<RDNode *>& phis) {
    while(!phis.empty()) {
        RDNode *phi = *(phis.begin());
        phis.erase(phis.begin());

        auto block = phi->getBBlock();
        for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
            assert(phi->overwrites.size() == 1);
            const auto& ds = *(phi->overwrites.begin());
            auto newphis = readValue(*I, phi, ds);
            assert(newphis.size() <= 1);

            // update phi nodes and block definitions
            for (auto phi : newphis) {
                I->prependAndUpdateCFG(phi);
                assert(I->definitions.get(ds).empty());
                I->definitions.update(ds, phi);
                // queue the new phi for processing
                phis.insert(phi);
            }
        }

    }
}

static void recGatherNonPhisDefs(RDNode *phi, std::set<RDNode *>& phis, std::set<RDNode *>& ret) {
    assert(phi->getType() == RDNodeType::PHI);
    if (!phis.insert(phi).second)
        return; // we already visited this phi

    for (auto n : phi->defuse) {
        if (n->getType() != RDNodeType::PHI) {
            ret.insert(n);
        } else {
            recGatherNonPhisDefs(n, phis, ret);
        }
    }
}

// recursivelu replace all phi values with its non-phi definitions
static std::vector<RDNode *> gatherNonPhisDefs(RDNode *use) {
    std::set<RDNode *> ret; // use set to get rid of duplicates
    std::set<RDNode *> phis; // set of visited phi nodes - to check the fixpoint

    for (auto n : use->defuse) {
        if (n->getType() != RDNodeType::PHI) {
            ret.insert(n);
        } else {
            recGatherNonPhisDefs(n, phis, ret);
        }
    }

    return std::vector<RDNode *>(ret.begin(), ret.end());
}

std::vector<RDNode *>
SSAReachingDefinitionsAnalysis::getReachingDefinitions(RDNode *use) {
    return gatherNonPhisDefs(use);
}

} // namespace rd
} // namespace analysis
} // namespace dg
