#include <set>
#include <vector>

#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/MemorySSA/MemorySSA.h"
#include "dg/analysis/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {

extern RWNode *UNKNOWN_MEMORY;

///
// Find the nodes that define the given def-site.
// Create PHI nodes if needed.
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWBBlock *block,
                                         const DefSite& ds) {
    // FIXME: the graph may contain dead code for which no blocks
    // are set (as the blocks are created only for the reachable code).
    // Removing the dead code is easy, but then we must somehow
    // adjust the mapping in the builder, which is not that straightforward.
    // Once we have that, uncomment this assertion.
    //assert(block && "Block is null");
    if (!block)
        return {};

    assert(ds.target && "Target is null");

    // Find known definitions.
    auto defSet = block->definitions.get(ds);
    std::vector<RWNode *> defs(defSet.begin(), defSet.end());

    // add definitions to unknown memory
    auto unknown = block->definitions.get({UNKNOWN_MEMORY, 0, Offset::UNKNOWN});
    defs.insert(defs.end(), unknown.begin(), unknown.end());

    // Find definitions that are not in this block (if any).
    auto uncovered = block->definitions.undefinedIntervals(ds);
    for (auto& interval : uncovered) {
        // if we have a unique predecessor, try finding definitions
        // and creating the new PHI nodes there.
        if (auto pred = block->getSinglePredecessor()) {
            auto pdefs = findDefinitions(pred, ds);
            defs.insert(defs.end(), pdefs.begin(), pdefs.end());
        } else {
            // Several predecessors -- we must create a PHI.
            // This phi is the definition that we are looking for.
            _phis.emplace_back(graph.create(RWNodeType::PHI));
            _phis.back()->addOverwrites(ds.target,
                                       interval.start,
                                       interval.length());
            // update definitions in the block -- this
            // phi node defines previously uncovered memory
            assert(block->definitions.get({ds.target,
                                           interval.start, interval.length()}
                                         ).empty());
            block->definitions.update({ds.target, interval.start, interval.length()},
                                      _phis.back());

            // Inserting at the beginning of the block should not
            // invalidate the iterator
            block->prependAndUpdateCFG(_phis.back());

            // this represents the sought definition
            defs.push_back(_phis.back());
        }
    }

    return defs;
}

///
// Find the nodes that define the given def-site.
// Create PHI nodes if needed. For LVN only.
std::vector<RWNode *>
MemorySSATransformation::findDefinitionsInBlock(RWBBlock *block,
                                                const DefSite& ds) {
    // get defs of known definitions
    auto defSet = block->definitions.get(ds);
    std::vector<RWNode *> defs(defSet.begin(), defSet.end());

    // add definitions to unknown memory
    auto unknown = block->definitions.get({UNKNOWN_MEMORY, 0, Offset::UNKNOWN});
    defs.insert(defs.end(), unknown.begin(), unknown.end());

    // find out which bytes are not covered yet
    // and create phi nodes for these intervals
    auto uncovered = block->definitions.undefinedIntervals(ds);
    for (auto& interval : uncovered) {
        _phis.emplace_back(graph.create(RWNodeType::PHI));
        _phis.back()->addOverwrites(ds.target,
                                    interval.start,
                                    interval.length());
        // update definitions in the block -- this
        // phi node defines previously uncovered memory
        assert(block->definitions.get({ds.target, interval.start, interval.length()}).empty());
        block->definitions.update({ds.target, interval.start, interval.length()},
                                  _phis.back());

        // Inserting at the beginning of the block should not
        // invalidate the iterator
        block->prependAndUpdateCFG(_phis.back());

        defs.push_back(_phis.back());
    }

    return defs;
}

void MemorySSATransformation::performLvn(RWBBlock *block) {
    // perform Lvn for one block
    for (RWNode *node : block->getNodes()) {
        // strong update
        for (auto& ds : node->overwrites) {
            assert(!ds.offset.isUnknown() && "Update on unknown offset");
            assert(!ds.target->isUnknown() && "Update on unknown memory");

            block->definitions.update(ds, node);
        }

        // weak update
        for (auto& ds : node->defs) {
            if (ds.target->isUnknown()) {
                // special handling for unknown memory
                // -- this node may define any memory that we know
                // about at this moment, so just add it to every
                // element of the definition map
                block->definitions.addAll(node);
                // also add the definition as a proper target for Gvn
                block->definitions.add({ds.target, 0, Offset::UNKNOWN}, node);
                continue;
            }

            // since this is just weak update,
            // look for the previous definitions of 'ds'
            // and if there are none, add a PHI node
            node->defuse.add(findDefinitionsInBlock(block, ds));

            // NOTE: this must be after findDefinitionsInBlock, otherwise
            // also this definition will be found
            block->definitions.add(ds, node);
        }

        // use
        for (auto& ds : node->uses) {
            node->defuse.add(findDefinitionsInBlock(block, ds));
        }
    }
}

void MemorySSATransformation::performLvn() {
    DBG_SECTION_BEGIN(dda, "Starting LVN");
    for (RWBBlock *block : graph.blocks()) {
        performLvn(block);
    }
    DBG_SECTION_END(dda, "LVN finished");
}

void MemorySSATransformation::performGvn() {
    DBG_SECTION_BEGIN(dda, "Starting GVN");
    std::set<RWNode *> phis(_phis.begin(), _phis.end());

    while(!phis.empty()) {
        RWNode *phi = *(phis.begin());
        phis.erase(phis.begin());

        // get the definition from the PHI node
        assert(phi->overwrites.size() == 1);
        const auto& ds = *(phi->overwrites.begin());

        auto block = phi->getBBlock();

        for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
            auto old_phis_size = _phis.size();

            // find definitions of this memory in the predecessor blocks
            phi->defuse.add(findDefinitions(*I, ds));

            // Queue the new phi (if any) for processing.
            if (_phis.size() != old_phis_size) {
                assert(_phis.size() > old_phis_size);
                for (auto i = old_phis_size; i < _phis.size(); ++i) {
                    phis.insert(_phis[i]);
                }
            }
        }
    }
    DBG_SECTION_END(dda, "GVN finished");

    DBG_SECTION_BEGIN(dda, "Caching reads of unknown memory (requested)");
    for (auto *block :graph.blocks()) {
        for (auto *node : block->getNodes()) {
            if (node->usesUnknown()) {
                findAllReachingDefinitions(node);
            }
        }
    }
    DBG_SECTION_END(dda, "Caching reads of unknown memory");
}

static void recGatherNonPhisDefs(RWNode *phi, std::set<RWNode *>& phis, std::set<RWNode *>& ret) {
    assert(phi->getType() == RWNodeType::PHI);
    if (!phis.insert(phi).second)
        return; // we already visited this phi

    for (auto n : phi->defuse) {
        if (n->getType() != RWNodeType::PHI) {
            ret.insert(n);
        } else {
            recGatherNonPhisDefs(n, phis, ret);
        }
    }
}

// recursivelu replace all phi values with its non-phi definitions
template <typename ContT>
std::vector<RWNode *> gatherNonPhisDefs(const ContT& nodes) {
    std::set<RWNode *> ret; // use set to get rid of duplicates
    std::set<RWNode *> phis; // set of visited phi nodes - to check the fixpoint

    for (auto n : nodes) {
        if (n->getType() != RWNodeType::PHI) {
            ret.insert(n);
        } else {
            recGatherNonPhisDefs(n, phis, ret);
        }
    }

    return std::vector<RWNode *>(ret.begin(), ret.end());
}

std::vector<RWNode *>
MemorySSATransformation::getDefinitions(RWNode *use) {
    if (use->usesUnknown())
        return findAllReachingDefinitions(use);

    return gatherNonPhisDefs(use->defuse);
}

std::vector<RWNode *>
MemorySSATransformation::findAllReachingDefinitions(RWNode *from) {
    DBG_SECTION_BEGIN(dda, "MemorySSA - finding all definitions");
    assert(from->getBBlock() && "The node has no BBlock");

    auto block = from->getBBlock();
    DefinitionsMap<RWNode> defs; // auxiliary map for finding defintions
    std::set<RWBBlock *> visitedBlocks; // for terminating the search

    ///
    // -- Get the definitions from predecessors --
    // NOTE: do not add block to visitedBlocks, it may be its own predecessor,
    // in which case we want to process it
    if (auto singlePred = block->getSinglePredecessor()) {
        findAllReachingDefinitions(defs, singlePred, visitedBlocks);
        // cache the found definitions
        singlePred->allDefinitions = defs;
    } else {
        // for multiple predecessors, we must create a copy of the
        // definitions that we have not found yet (a new copy for each
        // iteration. Here we create one redundant copy, but what the hell...)
        for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
            DefinitionsMap<RWNode> tmpDefs;
            findAllReachingDefinitions(tmpDefs, *I, visitedBlocks);

            defs.add(tmpDefs);
            (*I)->allDefinitions = std::move(tmpDefs);
        }
    }

    ///
    // get the definitions from this block (this is basically the LVN)
    // We do it after searching predecessors, because we cache the
    // definitions in predecessors.
    ///
    for (auto node : block->getNodes()) {
        // run only from the beginning of the block up to the node
        if (node == from)
            break;

        // weak update
        for (auto& ds : node->defs) {
            if (ds.target->isUnknown()) {
                defs.addAll(node);
                defs.add({ds.target, 0, Offset::UNKNOWN}, node);
                continue;
            }

            defs.add(ds, node);
        }

        // strong update
        for (auto& ds : node->overwrites) {
            defs.update(ds, node);
        }
    }

    std::set<RWNode *> foundDefs; // definitions that we found
    for (auto& it : defs) {
        for (auto& nds : it.second) {
            foundDefs.insert(nds.second.begin(), nds.second.end());
        }
    }

    ///
    // Gather all the defintions
    ///
    DBG_SECTION_END(dda, "MemorySSA - finding all definitions done");
    return gatherNonPhisDefs(foundDefs);
}

static void joinDefinitions(DefinitionsMap<RWNode>& from,
                            DefinitionsMap<RWNode>& to) {
    for (auto& it : from) {
        if (!to.definesTarget(it.first)) {
            // just copy the definitions
            to.add(it.first, it.second);
            continue;
        }

        for (auto& it2 : it.second) {
            auto& interv = it2.first;
            auto uncovered
                = to.undefinedIntervals({it.first, interv.start, interv.length()});
            for (auto& undefInterv : uncovered) {
                // we still do not have definitions for these bytes, add it
                to.add({it.first, undefInterv.start, undefInterv.length()}, it2.second);
            }
        }
    }
}

void
MemorySSATransformation::findAllReachingDefinitions(DefinitionsMap<RWNode>& defs,
                                                    RWBBlock *from,
                                                    std::set<RWBBlock *>& visitedBlocks) {
    if (!from)
        return;

    if (!visitedBlocks.insert(from).second) {
        // we already visited this block, therefore we have computed
        // all reaching definitions and we can re-use them
        joinDefinitions(from->allDefinitions, defs);
        return;
    }

    // we already computed all the definitions during some search?
    // Then use it.
    if (!from->allDefinitions.empty()) {
        joinDefinitions(from->allDefinitions, defs);
        return;
    }

    // get the definitions from this block
    joinDefinitions(from->definitions, defs);

    // recur into predecessors
    if (auto singlePred = from->getSinglePredecessor()) {
        findAllReachingDefinitions(defs, singlePred, visitedBlocks);
    } else {
        for (auto I = from->pred_begin(), E = from->pred_end(); I != E; ++I) {
            auto tmpDefs = defs;
            findAllReachingDefinitions(tmpDefs, *I, visitedBlocks);
            defs.add(tmpDefs);
        }
    }
}

} // namespace analysis
} // namespace dg
