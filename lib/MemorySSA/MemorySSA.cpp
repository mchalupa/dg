#include <set>
#include <vector>

#include "dg/ReachingDefinitions/RDMap.h"
#include "dg/MemorySSA/MemorySSA.h"
#include "dg/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

extern RWNode *UNKNOWN_MEMORY;

/// ------------------------------------------------------------------
// class Definitions
/// ------------------------------------------------------------------
void
MemorySSATransformation::Definitions::update(RWNode *node) {
    // possible definitions
    for (auto& ds : node->defs) {
        if (ds.target->isUnknown()) {
            // this makes all lastDefs into possibleDefs,
            // since we do not know if it was defined here or there
            // also add the definition as a proper target for Gvn
            definitions.addAll(node);
            addUnknownWrite(node);
        } else {
            definitions.add(ds, node);
        }
    }

    // definitive definitions
    for (auto& ds : node->overwrites) {
        assert((node->getType() == RWNodeType::PHI || // we allow ? for PHI nodes
               !ds.offset.isUnknown()) && "Update on unknown offset");
        assert(!ds.target->isUnknown() && "Update on unknown memory");

        kills.add(ds, node);
        definitions.update(ds, node);
    }

    // gather unknown uses
    if (node->usesUnknown()) {
        addUnknownRead(node);
    }
}


/// ------------------------------------------------------------------
// class MemorySSATransformation
/// ------------------------------------------------------------------

// find definitions of a given node
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWNode *node) {
    assert(node->isUse() && "Searching definitions for non-use node");

    // handle reads from unknown memory
    if (node->usesUnknown()) {
        return findAllReachingDefinitions(node);
    }

    auto block = node->getBBlock();
    // FIXME: the graph may contain dead code for which no blocks
    // are set (as the blocks are created only for the reachable code).
    // Removing the dead code is easy, but then we must somehow
    // adjust the mapping in the builder, which is not that straightforward.
    // Once we have that, uncomment this assertion.
    //assert(block && "Block is null");
    if (!block)
        return {};

    // gather all definitions from the beginning of the block
    // to the node (we must do that always, because adding PHI
    // nodes changes the definitions)
    auto D = findDefinitionsInBlock(node);
    std::vector<RWNode *> defs;

    for (auto& ds : node->uses) {
        assert(ds.target && "Target is null");

        // add the definitions from the beginning of this block to the defs container
        auto defSet = D.definitions.get(ds);
        if (defSet.empty()) {
            defs.insert(defs.end(),
                        D.getUnknownWrites().begin(),
                        D.getUnknownWrites().end());
        } else {
            defs.insert(defs.end(), defSet.begin(), defSet.end());
        }

        auto uncovered = D.uncovered(ds);
        for (auto& interval : uncovered) {
            auto preddefs
                = findDefinitionsInPredecessors(block, {ds.target,
                                                        interval.start,
                                                        interval.length()});
            defs.insert(defs.end(), preddefs.begin(), preddefs.end());
        }
    }

    return defs;
}

///
// Add found definitions 'found' from a block to 'defs'.
// Account for the cases when we found nothing and therefore we
// want to add writes to unknown memory
template <typename FoundT, typename DefsT> void
addFoundDefinitions(std::vector<RWNode *>& defs,
                    const FoundT& found,
                    DefsT& D) {
    if (found.empty()) {
        defs.insert(defs.end(),
                    D.getUnknownWrites().begin(),
                    D.getUnknownWrites().end());
    } else {
        defs.insert(defs.end(), found.begin(), found.end());
    }
}

///
// Find the nodes that define the given def-site in the predecessors
// of block.  Create PHI nodes if needed.
std::vector<RWNode *>
MemorySSATransformation::findDefinitionsInPredecessors(RWBBlock *block,
                                                       const DefSite& ds) {
    assert(block);
    assert(ds.target && "Target is null");
    assert(!ds.target->isUnknown() && "Finding uknown memory"); // this is handled differently

    std::vector<RWNode *> defs;

    // if we have a unique predecessor,
    // we can find the definitions there and continue searching in the predecessor
    // if something is missing
    if (auto pred = block->getSinglePredecessor()) {
        auto pdefs = findDefinitions(pred, ds);
        auto& D = _defs[pred];

        addFoundDefinitions(defs, pdefs, D);

        auto uncovered = D.uncovered(ds);
        for (auto& interval : uncovered) {
            auto preddefs
                = findDefinitionsInPredecessors(pred, {ds.target,
                                                       interval.start,
                                                       interval.length()});
            defs.insert(defs.end(), preddefs.begin(), preddefs.end());
        }
    } else { // multiple predecessors
        // create PHI node and find definitions for the PHI node
        auto& D = _defs[block];

        // This phi is the definition that we are looking for.
        _phis.emplace_back(&graph.create(RWNodeType::PHI));
        _phis.back()->addOverwrites(ds);
        // update definitions in the block -- this
        // phi node defines previously uncovered memory
        auto uncovered = D.uncovered(ds);
        for (auto& interval : uncovered) {
            DefSite uds{ds.target, interval.start, interval.length()};
            assert(D.kills.get(uds).empty());
            D.definitions.update(uds, _phis.back());
            D.kills.add(uds, _phis.back());

            // to simulate the whole LVN, we must add also writes to unknown memory
            if (!D.getUnknownWrites().empty()) {
                D.definitions.add(uds, D.getUnknownWrites());
            }
        }

        // Inserting at the beginning of the block should not
        // invalidate the iterator
        block->prependAndUpdateCFG(_phis.back());

        // this represents the sought definition
        defs.push_back(_phis.back());

        findPhiDefinitions(_phis.back());
    }

    return defs;
}

///
// Find the nodes that define the given def-site in this block
// (using the definitions computed for each single block)
// Create PHI nodes if needed.
void MemorySSATransformation::findPhiDefinitions(RWNode *phi) {
    auto block = phi->getBBlock();

    assert(block);
    assert(!block->getSinglePredecessor() &&
           "Phi in a block with single predecessor");

    std::set<RWNode *> defs;

    assert(phi->overwrites.size() == 1);
    const auto& ds = *(phi->overwrites.begin());
    // we handle this case separately
    assert(!ds.target->isUnknown() && "PHI for unknown memory");

    for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
        auto tmpdefs = findDefinitions(*I, ds);
        defs.insert(tmpdefs.begin(), tmpdefs.end());
    }

    phi->defuse.add(defs);
}


///
// Find the nodes that define the given def-site in this block
// (using the definitions computed for each single block)
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
    auto& D = _defs[block];

    if (!D.allDefinitions.empty()) { // do we have a cache?
        auto defSet = D.allDefinitions.get(ds);
        return std::vector<RWNode *>{defSet.begin(), defSet.end()};
    }

    // XXX: wrap this into a get() method of Definitions
    auto defSet = D.definitions.get(ds);
    std::vector<RWNode *> defs(defSet.begin(), defSet.end());

    addFoundDefinitions(defs, defSet, D);

    auto uncovered = D.uncovered(ds);
    for (auto& interval : uncovered) {
        auto preddefs
            = findDefinitionsInPredecessors(block, {ds.target,
                                                    interval.start,
                                                    interval.length()});
        defs.insert(defs.end(), preddefs.begin(), preddefs.end());
    }

    return defs;
}

// perform Lvn for one block
void MemorySSATransformation::performLvn(RWBBlock *block) {
    auto& D = _defs[block];
    for (RWNode *node : block->getNodes()) {
        D.update(node);
    }

    D.setProcessed();
}

///
// The same as LVN but only up to some point (and returns the map)
MemorySSATransformation::Definitions
MemorySSATransformation::findDefinitionsInBlock(RWNode *to) {
    auto block = to->getBBlock();
    // perform LVN up to the node
    Definitions D;
    for (RWNode *node : block->getNodes()) {
        if (node == to)
            break;
        D.update(node);
    }

    return D;
}


// perform Lvn on all blocks
void MemorySSATransformation::performLvn() {
    DBG_SECTION_BEGIN(dda, "Starting LVN");
    for (auto *subgraph : graph.subgraphs()) {
        for (RWBBlock *block : subgraph->bblocks()) {
            performLvn(block);
        }
    }
    DBG_SECTION_END(dda, "LVN finished");
}

// take each use and compute def-use edges (adding PHI nodes if needed)
void MemorySSATransformation::performGvn() {
    DBG_SECTION_BEGIN(dda, "Starting GVN");
    std::set<RWNode *> phis(_phis.begin(), _phis.end());

    for (auto *subgraph : graph.subgraphs()) {
        for (RWBBlock *block : subgraph->bblocks()) {
            for (RWNode *node : block->getNodes()) {
                if (node->isUse())
                    node->defuse.add(findDefinitions(node));
            }
        }
    }
    DBG_SECTION_END(dda, "GVN finished");

/*
    DBG_SECTION_BEGIN(dda, "Caching reads of unknown memory (requested)");
    for (auto *block :graph.blocks()) {
        for (auto *node : block->getNodes()) {
            if (node->usesUnknown()) {
                findAllReachingDefinitions(node);
            }
        }
    }
    DBG_SECTION_END(dda, "Caching reads of unknown memory");
*/
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
    return gatherNonPhisDefs(use->defuse);
}

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RWNode *>
MemorySSATransformation::getDefinitions(RWNode *where,
                                        RWNode *mem,
                                        const Offset& off,
                                        const Offset& len) {
    //DBG_SECTION_BEGIN(dda, "Adding MU node");
    auto use = insertUse(where, mem, off, len);
    use->defuse.add(findDefinitions(use));
    //DBG_SECTION_END(dda, "Created MU node " << use->getID());
    return gatherNonPhisDefs(use->defuse);
}

RWNode *MemorySSATransformation::insertUse(RWNode *where, RWNode *mem,
                                           const Offset& off, const Offset& len) {
    auto& use = graph.create(RWNodeType::MU);
    use.addUse({mem, off, len});
    use.insertBefore(where);
    where->getBBlock()->insertBefore(&use, where);

    return &use;
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
        joinDefinitions(_defs[from].allDefinitions, defs);
        return;
    }

    // we already computed all the definitions during some search?
    // Then use it.
    auto& D = _defs[from];
    if (!D.allDefinitions.empty()) {
        joinDefinitions(D.allDefinitions, defs);
        return;
    }

    // get the definitions from this block
    joinDefinitions(D.definitions, defs);

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

std::vector<RWNode *>
MemorySSATransformation::findAllReachingDefinitions(RWNode *from) {
    DBG_SECTION_BEGIN(dda, "MemorySSA - finding all definitions");
    assert(from->getBBlock() && "The node has no BBlock");

    auto block = from->getBBlock();
    DefinitionsMap<RWNode> defs; // auxiliary map for finding defintions
    std::set<RWBBlock *> visitedBlocks; // for terminating the search

    auto D = findDefinitionsInBlock(from);

    ///
    // -- Get the definitions from predecessors --
    // NOTE: do not add block to visitedBlocks, it may be its own predecessor,
    // in which case we want to process it
    if (auto singlePred = block->getSinglePredecessor()) {
        // NOTE: we must start with emtpy defs,
        // to gather all reaching definitions (due to caching)
        assert(defs.empty());
        findAllReachingDefinitions(defs, singlePred, visitedBlocks);
        // cache the found definitions
        _defs[singlePred].allDefinitions = defs;
    } else {
        // for multiple predecessors, we must create a copy of the
        // definitions that we have not found yet (a new copy for each
        // iteration.
        // NOTE: no caching here...
        for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
            DefinitionsMap<RWNode> tmpDefs = D.kills; // do not search for what we have already
            findAllReachingDefinitions(tmpDefs, *I, visitedBlocks);
            defs.add(tmpDefs);
            // NOTE: we cannot catch here because of the DFS nature of the search
            // (the found definitions does not contain _all_ reaching definitions)
            //_defs[*I].allDefinitions = std::move(tmpDefs);
        }
    }

    // create the final map of definitions reaching the 'from' node
    joinDefinitions(defs, D.definitions);
    defs.swap(D.definitions);

    ///
    // get the definitions from this block (this is basically the LVN)
    // We do it after searching predecessors, because we cache the
    // definitions in predecessors.
    ///

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

void MemorySSATransformation::run() {
    DBG_SECTION_BEGIN(dda, "Running MemorySSA analysis");

    // graph.buildBBlocks();
    // _defs.reserve(graph.getBBlocks().size());

    performLvn();
    performGvn();

    DBG_SECTION_END(dda, "Running MemorySSA analysis finished");
}

} // namespace dda
} // namespace dg
