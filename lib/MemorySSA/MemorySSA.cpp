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
///
// Update Definitions object with definitions from 'node'.
// 'defnode' is the node that should be added as the
// node that performs these definitions. Usually, defnode == node
// (defnode was added to correctly handle call nodes where the
// definitions are stored separately, but we want to have the call node
// as the node that defines the memory.
///
void
MemorySSATransformation::Definitions::update(RWNode *node, RWNode *defnode) {
    if (!defnode)
        defnode = node;

    // possible definitions
    for (auto& ds : node->defs) {
        if (ds.target->isUnknown()) {
            // this makes all lastDefs into possibleDefs,
            // since we do not know if it was defined here or there
            // also add the definition as a proper target for Gvn
            definitions.addAll(defnode);
            addUnknownWrite(defnode);
        } else {
            definitions.add(ds, defnode);
        }
    }

    // definitive definitions
    for (auto& ds : node->overwrites) {
        assert((defnode->getType() == RWNodeType::PHI || // we allow ? for PHI nodes
               !ds.offset.isUnknown()) && "Update on unknown offset");
        assert(!ds.target->isUnknown() && "Update on unknown memory");

        kills.add(ds, defnode);
        definitions.update(ds, defnode);
    }

    // gather unknown uses
    if (node->usesUnknown()) {
        addUnknownRead(defnode);
    }
}

/// ------------------------------------------------------------------
// class MemorySSATransformation
/// ------------------------------------------------------------------


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
        // gather the found definitions (these include also the unknown memory)
        defs.insert(defs.end(), found.begin(), found.end());
    }
}

// find definitions of a given node
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWNode *node) {
    DBG(dda, "Searching definitions for node " << node->getID());

    assert(node->isUse() && "Searching definitions for non-use node");

    // handle reads from unknown memory
    if (node->usesUnknown()) {
        return findAllReachingDefinitions(node);
    }

    auto *block = node->getBBlock();
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
        addFoundDefinitions(defs, defSet, D);

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
        auto& D = getBBlockDefinitions(pred);

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
        auto& D = getBBlockDefinitions(block);

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
    auto& D = getBBlockDefinitions(block);

    if (hasCachedDefinitions(block)) { // do we have a cache?
        auto defSet = getCachedDefinitions(block).get(ds);
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

DefinitionsMap<RWNode>&
MemorySSATransformation::getCachedDefinitions(RWBBlock *b) {
    return _cached_defs[b];
}

MemorySSATransformation::Definitions&
MemorySSATransformation::getBBlockDefinitions(RWBBlock *b) {
    auto& D = _defs[b];
    if (!D.isProcessed()) {
        performLvn(D, b);
    }
    return D;
}


// perform Lvn for one block
void MemorySSATransformation::performLvn(Definitions& D, RWBBlock *block) {
    DBG_SECTION_BEGIN(dda, "Starting LVN for " << block);

    assert(!D.isProcessed() && "Processing a block multiple times");

    for (RWNode *node : block->getNodes()) {
        D.update(node);
   }

    D.setProcessed();
    DBG_SECTION_END(dda, "LVN finished");
}

///
// The same as performLVN() but only up to some point (and returns the map)
MemorySSATransformation::Definitions
MemorySSATransformation::findDefinitionsInBlock(RWNode *to) {
    auto *block = to->getBBlock();
    // perform LVN up to the node
    Definitions D;
    for (RWNode *node : block->getNodes()) {
        if (node == to)
            break;
        D.update(node);
    }

    return D;
}

// take each use and compute def-use edges (adding PHI nodes if needed)
void MemorySSATransformation::performGvn(RWSubgraph *subgraph) {
    for (RWBBlock *block : subgraph->bblocks()) {
        for (RWNode *node : block->getNodes()) {
            if (node->isUse())
                node->defuse.add(findDefinitions(node));
        }
    }
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
    auto *use = insertUse(where, mem, off, len);
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

///
/// Copy definitions from 'from' map to 'to' map.
/// Copy only those that are not already killed by 'to' map
/// (thus simulating the state when 'to' is executed after 'from')
///
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
        joinDefinitions(getCachedDefinitions(from), defs);
        return;
    }

    // we already computed all the definitions during some search? Then use it.
    auto& D = getBBlockDefinitions(from);
    if (hasCachedDefinitions(from)) {
        joinDefinitions(getCachedDefinitions(from), defs);
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

    auto *block = from->getBBlock();
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
        // FIXME: optimize the accesses
        assert(!hasCachedDefinitions(singlePred));
        getCachedDefinitions(singlePred) = defs;
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
    graph.splitBBlocksOnCalls();

    // XXX: maybe we could have _defs per a subgraph?

    // perform also GVN on the entry subgraph
    performGvn(graph.getEntry());

    DBG_SECTION_END(dda, "Running MemorySSA analysis finished");
}

} // namespace dda
} // namespace dg
