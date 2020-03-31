#include <set>
#include <vector>

#include "dg/MemorySSA/MemorySSA.h"
//#include "dg/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

static inline RWNode::Annotations& getAnnotations(RWNode *node) {
    return node->getAnnotations();
}

static inline DefSiteSetT& getDefines(RWNode *node) {
    return getAnnotations(node).getDefines();
}

static inline DefSiteSetT& getOverwrites(RWNode *node) {
    return getAnnotations(node).getOverwrites();
}

static inline DefSiteSetT& getUses(RWNode *node) {
    return getAnnotations(node).getUses();
}

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
    for (auto& ds : getDefines(node)) {
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
    for (auto& ds : getOverwrites(node)) {
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
        return findAllDefinitions(node);
    }

    auto *block = node->getBBlock();
    assert(block && "Block is null");
    //if (!block)
    //    return {};

    // gather all definitions from the beginning of the block
    // to the node (we must do that always, because adding PHI
    // nodes changes the definitions)
    auto D = findDefinitionsInBlock(node);
    std::vector<RWNode *> defs;

    for (auto& ds : getUses(node)) {
        assert(ds.target && "Target is null");

        // add the definitions from the beginning of this block to the defs container
        auto defSet = D.definitions.get(ds);
        addFoundDefinitions(defs, defSet, D);

        addUncoveredFromPredecessors(block, D, ds, defs);
    }

    return defs;
}

RWNode *MemorySSATransformation::createPhi(const DefSite& ds) {
    // This phi is the definition that we are looking for.
    _phis.emplace_back(&graph.create(RWNodeType::PHI));
    auto *phi = _phis.back();

    phi->addOverwrites(ds);

    DBG(dda, "Created PHI with ID " << phi->getID());
    return phi;
}


RWNode *MemorySSATransformation::createPhi(Definitions& D, const DefSite& ds) {
    auto *phi = createPhi(ds);

    // update definitions in the block -- this
    // phi node defines previously uncovered memory
    auto uncovered = D.uncovered(ds);
    for (auto& interval : uncovered) {
        DefSite uds{ds.target, interval.start, interval.length()};
        assert(D.kills.get(uds).empty()
               && "BUG: Basic block already kills this memory");
        D.definitions.update(uds, phi);
        D.kills.add(uds, phi);

        // to simulate the whole LVN, we must add also writes to unknown memory
        if (!D.getUnknownWrites().empty()) {
            D.definitions.add(uds, D.getUnknownWrites());
        }
    }

    return phi;
}

RWNode *MemorySSATransformation::createAndPlacePhi(RWBBlock *block, const DefSite& ds) {
    // create PHI node and find definitions for the PHI node
    auto& D = getBBlockDefinitions(block, &ds);
    auto *phi = createPhi(D, ds);
    block->prepend(phi);
    return phi;
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
        auto& D = getBBlockDefinitions(pred, &ds);
        addFoundDefinitions(defs, pdefs, D);
        addUncoveredFromPredecessors(pred, D, ds, defs);

    } else { // multiple or no predecessors
        RWNode *phi;
        if (block->hasPredecessors()) {
            phi = createAndPlacePhi(block, ds);
            // The phi node will be placed at the beginning of the block,
            // so the iterator should not be invalidated
            // this represents the sought definition
            // recursively find definitions for this phi node
            findPhiDefinitions(phi);
        } else {
            phi = createPhi(getBBlockDefinitions(block, &ds), ds);
            auto *subg = block->getSubgraph();
            auto& summary = getSubgraphSummary(subg);
            summary.addInput(ds, phi);
            findDefinitionsFromCalledFun(phi, subg, ds);
        }

        defs.push_back(phi);
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

    findPhiDefinitions(phi, block->getPredecessors());
}

void MemorySSATransformation::addUncoveredFromPredecessors(
                                        RWBBlock *block,
                                        MemorySSATransformation::Definitions& D,
                                        const DefSite& ds,
                                        std::vector<RWNode *>& defs) {
    auto uncovered = D.uncovered(ds);
    for (auto& interval : uncovered) {
        auto preddefs
            = findDefinitionsInPredecessors(block, {ds.target,
                                                    interval.start,
                                                    interval.length()});
        defs.insert(defs.end(), preddefs.begin(), preddefs.end());
    }
}


///
// Find the nodes that define the given def-site in this block
// (using the definitions computed for each single block)
// Create PHI nodes if needed.
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWBBlock *block,
                                         const DefSite& ds) {
    assert(block && "Block is null");
    //if (!block)
    //    return {};

    assert(ds.target && "Target is null");

    if (hasCachedDefinitions(block)) { // do we have a cache?
        auto defSet = getCachedDefinitions(block).get(ds);
        return std::vector<RWNode *>{defSet.begin(), defSet.end()};
    }

    // Find known definitions.
    auto& D = getBBlockDefinitions(block, &ds);

    // XXX: wrap this into a get() method of Definitions
    auto defSet = D.definitions.get(ds);
    std::vector<RWNode *> defs(defSet.begin(), defSet.end());

    addFoundDefinitions(defs, defSet, D);

    addUncoveredFromPredecessors(block, D, ds, defs);

    return defs;
}

DefinitionsMap<RWNode>&
MemorySSATransformation::getCachedDefinitions(RWBBlock *b) {
    return _cached_defs[b];
}

void MemorySSATransformation::findDefinitionsFromCall(Definitions& D,
                                                      RWNodeCall *C,
                                                      const DefSite& ds) {
    // check if we have found such a definitions before
    auto uncovered = D.uncovered(ds);
    for (auto& interval : uncovered) {
        auto uncoveredds = DefSite{ds.target, interval.start, interval.length()};
        // this phi will merge the definitions from all the
        // possibly called subgraphs
        auto *phi = createPhi(D, uncoveredds);
        C->getBBlock()->append(phi);

        // recursively find definitions for this phi node
        for (auto& callee : C->getCallees()) {
            auto *subg = callee.getSubgraph();
            // FIXME: add support for mixing the values
            assert(subg && "Undefined values mixed with subgraph yet undefined");

            auto& summary = getSubgraphSummary(subg);
            // we must create a new phi for each subgraph (these phis will
            // be merged by the single phi created at the beginning of this
            // method. Of course, we create them only when not already present.
            for (auto& subginterval : summary.getUncoveredOutputs(ds)) {
                auto *subgphi = createPhi(uncoveredds);
                summary.addOutput(DefSite{uncoveredds.target,
                                          subginterval.start,
                                          subginterval.length()},
                                  subgphi);

                // find the new phi operands
                for (auto *subgblock : subg->bblocks()) {
                    if (subgblock->hasSuccessors()) {
                        continue;
                    }
                    subgphi->addDefUse(findDefinitions(subgblock, uncoveredds));
                }
            }

            phi->addDefUse(summary.getOutputs(uncoveredds));
        }
    }
}

// get all callers of the function and find the given definitions reaching these
// call-sites
void MemorySSATransformation::findDefinitionsFromCalledFun(RWNode *phi,
                                                           RWSubgraph *subg,
                                                           const DefSite& ds) {
    // get call-sites of this
    for (auto *callsite : subg->getCallers()) {
        auto *bblock = callsite->getBBlock();
        assert(bblock && getBBlockInfo(bblock).isCallBlock());

        auto D = findDefinitionsInBlock(callsite, ds.target);
        std::vector<RWNode *> defs;
        addUncoveredFromPredecessors(bblock, D, ds, defs);
        // FIXME: we could move the defintions
        phi->addDefUse(defs);
    }
}


///
/// Get Definitions object for a bblock. Optionally, a definition site due to
///  which we are getting the definitions can be specified (used when searching
///  in call blocks on demand)
///
MemorySSATransformation::Definitions&
MemorySSATransformation::getBBlockDefinitions(RWBBlock *b, const DefSite *ds) {
    auto& bi = getBBlockInfo(b);
    auto& D = bi.getDefinitions();

    if (bi.isCallBlock()) {
        if (ds) {
            findDefinitionsFromCall(D, bi.getCall(), *ds);
        } else {
            findAllDefinitionsFromCall(D, bi.getCall());
        }
    } else {
        // normal basic block
        if (!D.isProcessed()) {
            performLvn(D, b);
        }
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
MemorySSATransformation::findDefinitionsInBlock(RWNode *to, const RWNode *mem) {
    auto *block = to->getBBlock();
    // perform LVN up to the node
    Definitions D;
    for (RWNode *node : block->getNodes()) {
        if (node == to)
            break;
        if (!mem || node->defines(mem)) {
            D.update(node);
        }
    }

    return D;
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
    // on demand triggering finding the definitions
    if (!use->defuse.initialized()) {
        use->addDefUse(findDefinitions(use));
        assert(use->defuse.initialized());
    }
    return gatherNonPhisDefs(use->defuse);
}

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RWNode *>
MemorySSATransformation::getDefinitions(RWNode *where,
                                        RWNode *mem,
                                        const Offset& off,
                                        const Offset& len) {
    auto *use = insertUse(where, mem, off, len);
    return getDefinitions(use);
}

RWNode *MemorySSATransformation::insertUse(RWNode *where, RWNode *mem,
                                           const Offset& off, const Offset& len) {
    //DBG_SECTION_BEGIN(dda, "Adding MU node");
    auto& use = graph.create(RWNodeType::MU);
    use.addUse({mem, off, len});
    use.insertBefore(where);
    where->getBBlock()->insertBefore(&use, where);
    //DBG_SECTION_END(dda, "Created MU node " << use->getID());

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

void MemorySSATransformation::findAllDefinitionsFromCall(Definitions& D,
                                                         RWNodeCall *C) {

    assert(false && "Not implemented");
}

void
MemorySSATransformation::collectAllDefinitions(DefinitionsMap<RWNode>& defs,
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
    if (hasCachedDefinitions(from)) {
        joinDefinitions(getCachedDefinitions(from), defs);
        return;
    }

    // get the definitions from this block
    joinDefinitions(getBBlockDefinitions(from).definitions, defs);

    // recur into predecessors
    if (auto singlePred = from->getSinglePredecessor()) {
        collectAllDefinitions(defs, singlePred, visitedBlocks);
    } else {
        for (auto I = from->pred_begin(), E = from->pred_end(); I != E; ++I) {
            auto tmpDefs = defs;
            collectAllDefinitions(tmpDefs, *I, visitedBlocks);
            defs.add(tmpDefs);
        }
    }
}

DefinitionsMap<RWNode>
MemorySSATransformation::collectAllDefinitions(RWNode *from) {
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
        collectAllDefinitions(defs, singlePred, visitedBlocks);
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
            collectAllDefinitions(tmpDefs, *I, visitedBlocks);
            defs.add(tmpDefs);
            // NOTE: we cannot cache here because of the DFS nature of the search
            // (the found definitions does not contain _all_ reaching definitions)
        }
    }

    // create the final map of definitions reaching the 'from' node
    joinDefinitions(defs, D.definitions);
    defs.swap(D.definitions);

    return defs;
}

std::vector<RWNode *>
MemorySSATransformation::findAllDefinitions(RWNode *from) {
    DBG_SECTION_BEGIN(dda, "MemorySSA - finding all definitions");

    auto defs = collectAllDefinitions(from);
    std::set<RWNode *> foundDefs; // definitions that we found
    for (auto& it : defs) {
        for (auto& nds : it.second) {
            foundDefs.insert(nds.second.begin(), nds.second.end());
        }
    }

    DBG_SECTION_END(dda, "MemorySSA - finding all definitions done");
    return gatherNonPhisDefs(foundDefs);
}

void MemorySSATransformation::computeAllDefinitions() {
    DBG_SECTION_BEGIN(dda, "Computing definitions for all uses (requested)");
    for (auto *subg : graph.subgraphs()) {
        for (auto *b : subg->bblocks()) {
            for (auto *n : b->getNodes()) {
                if (n->isUse()) {
                    if (!n->defuse.initialized()) {
                        n->addDefUse(findDefinitions(n));
                        assert(n->defuse.initialized());
                    }
                }
            }
        }
    }
    DBG_SECTION_END(dda, "Computing definitions for all uses finished");
}

void MemorySSATransformation::initialize() {
    // we need each call (of a defined function) in its own basic block
    graph.splitBBlocksOnCalls();
    // remove useless blocks and nodes
    graph.optimize();

    // make sure we have a constant-time access to information
    _subgraphs_info.reserve(graph.size());

    for (auto *subg : graph.subgraphs()) {
        auto& si = _subgraphs_info[subg];
        si._bblock_infos.reserve(subg->size());

        // initialize information about basic blocks
        for (auto *bb : subg->bblocks()) {
            if (bb->size() == 1) {
                if (auto *C = RWNodeCall::get(bb->getFirst())) {
                    if (C->callsDefined()) {
                        si._bblock_infos[bb].setCallBlock(C);
                    }
                }
            }
        }
    }
}

void MemorySSATransformation::run() {
    DBG_SECTION_BEGIN(dda, "Initializing MemorySSA analysis");

    initialize();

    // the rest is on-demand :)

    DBG_SECTION_END(dda, "Initializing MemorySSA analysis finished");
}

} // namespace dda
} // namespace dg
