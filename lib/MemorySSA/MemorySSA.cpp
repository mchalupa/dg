#include <set>
#include <vector>

#include "dg/MemorySSA/MemorySSA.h"
//#include "dg/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

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
    if (!block) {
        // no basic block means that this node is either
        // a subnode of a CALL node or that it is
        // in unreachable part of program (and therefore
        // the block was not built).
        // In either case, we're safe to return nothing
        return {};
    }

    // gather all definitions from the beginning of the block
    // to the node (we must do that always, because adding PHI
    // nodes changes the definitions)
    auto D = findDefinitionsInBlock(node);
    std::vector<RWNode *> defs;

    for (auto& ds : node->getUses()) {
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
        // NOTE: add, not update! D can already have some weak definitions
        // of this memory (but they are "uncovered" as they are only weak
        D.definitions.add(uds, phi);
        assert(D.kills.get(uds).empty()
               && "BUG: Basic block already kills this memory");
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

static inline bool canEscape(const RWNode *node) {
    return (node->getType() == RWNodeType::DYN_ALLOC ||
            node->getType() == RWNodeType::GLOBAL ||
            node->hasAddressTaken());
}

static inline bool canBeInput(const RWNode *node, RWSubgraph *subg) {
    // can escape or already escaped
    if (canEscape(node))
        return true;
    if (node->getBBlock())
        return node->getBBlock()->getSubgraph() != subg;
    return false;
}

void
MemorySSATransformation::findDefinitionsInMultiplePredecessors(RWBBlock *block,
                                                               const DefSite& ds,
                                                               std::vector<RWNode *>& defs) {
    RWNode *phi = nullptr;
    if (block->hasPredecessors()) {
        // The phi node will be placed at the beginning of the block,
        // so the iterator should not be invalidated.
        phi = createAndPlacePhi(block, ds);
        // recursively find definitions for this phi node
        findPhiDefinitions(phi);
    } else if (canBeInput(ds.target, block->getSubgraph())){
        // this is the entry block, so we add a PHI node
        // representing "input" into this procedure
        // (but only if the input can be used from the called procedure)
        phi = createPhi(getBBlockDefinitions(block, &ds), ds);
        auto *subg = block->getSubgraph();
        auto& summary = getSubgraphSummary(subg);
        summary.addInput(ds, phi);

        findDefinitionsFromCalledFun(phi, subg, ds);
    }

    if (phi) {
        defs.push_back(phi);
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
        auto& D = getBBlockDefinitions(pred, &ds);
        addFoundDefinitions(defs, pdefs, D);
        addUncoveredFromPredecessors(pred, D, ds, defs);
    } else { // multiple or no predecessors
        findDefinitionsInMultiplePredecessors(block, ds, defs);
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
    assert(ds.target && "Target is null");
    assert(block && "Block is null");
    //if (!block)
    //    return {};

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
    // find the uncovered parts of the sought definition
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
            if (!subg) {
                std::vector<RWNode *> defs;
                Definitions D;
                // FIXME: cache this somehow ?
                D.update(callee.getCalledValue());

                auto defSet = D.definitions.get(uncoveredds);
                addFoundDefinitions(defs, defSet, D);
                addUncoveredFromPredecessors(C->getBBlock(), D, uncoveredds, defs);
                phi->addDefUse(defs);
                continue;
            }

            auto& summary = getSubgraphSummary(subg);
            // we must create a new phi for each subgraph inside the subgraph
            // (these phis will be merged by the single phi created at the beginning
            // of this method. Of course, we create them only when not already present.
            for (auto& subginterval : summary.getUncoveredOutputs(uncoveredds)) {
                auto subgds = DefSite{uncoveredds.target,
                                      subginterval.start,
                                      subginterval.length()};
                auto *subgphi = createPhi(subgds);
                summary.addOutput(subgds, subgphi);

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
///  in call blocks on demand).
/// The on-demand for calls was such that we retrive the sought definitions
/// and store them into the Definitions object. This object is then
/// returned and can be queried.
///
MemorySSATransformation::Definitions&
MemorySSATransformation::getBBlockDefinitions(RWBBlock *b, const DefSite *ds) {
    auto& bi = getBBlockInfo(b);
    auto& D = bi.getDefinitions();

    if (D.isProcessed())
        return D;

    if (bi.isCallBlock()) {
        if (ds) {
            findDefinitionsFromCall(D, bi.getCall(), *ds);
        } else {
            findAllDefinitionsFromCall(D, bi.getCall());
            assert(D.isProcessed());
        }
    } else {
        performLvn(D, b); // normal basic block
        assert(D.isProcessed());
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
// The same as performLVN() but only up to some point (and returns the map).
// Also, if mem is specified, then search for effects only to this memory.
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

static inline bool canBeOutput(const RWNode *node, RWSubgraph *subg) {
    // can escape or already escaped
    return canEscape(node) ||
            (!node->getBBlock() || node->getBBlock()->getSubgraph() != subg);
}

template <typename MR, typename C>
static void modRefAdd(MR& modref, const C& c, RWNode *node, RWSubgraph *subg) {
    assert(node && "Node the definion node");
    for (const DefSite& ds : c) {
        // can escape
        if (canBeOutput(ds.target, subg)) {
            modref.add(ds, node);
        }
    }
}

void MemorySSATransformation::computeModRef(RWSubgraph *subg, SubgraphInfo& si) {

    if (si.modref.isInitialized()) {
        return;
    }

    DBG_SECTION_BEGIN(dda, "Computing modref for subgraph " << subg->getName());

    // set it here due to recursive procedures
    si.modref.setInitialized();

    // iterate over the blocks (note: not over the infos, those
    // may not be created if the block was not used yet
    for (auto *b : subg->bblocks()) {
        auto& bi = si.getBBlockInfo(b);
        if (bi.isCallBlock()) {
            // if the block is a call bblock, we must
            // compute all the reaching definitions first,
            // so that we have the modref info for the call
            // (calling getBBlockDefinitions() will trigger computing
            //  computing modref on demand)
            getBBlockDefinitions(b, nullptr);

            auto *C = bi.getCall();
            for (auto& callee : C->getCallees()) {
                auto *subg = callee.getSubgraph();
                if (subg) {
                    auto& callsi = getSubgraphInfo(subg);
                    assert(callsi.modref.isInitialized());
                    si.modref.add(callsi.modref);
                } else {
                    // undefined function
                    modRefAdd(si.modref.maydef,
                              callee.getCalledValue()->getDefines(),
                              C, subg);
                    modRefAdd(si.modref.maydef,
                              callee.getCalledValue()->getOverwrites(),
                              C, subg);
                    modRefAdd(si.modref.mayref,
                              callee.getCalledValue()->getUses(),
                              C, subg);
                }
            }
        } else {
            // do not perform LVN if not needed, just scan the nodes
            for (auto *node : b->getNodes()) {
                modRefAdd(si.modref.maydef, node->getDefines(), node, subg);
                modRefAdd(si.modref.maydef, node->getOverwrites(), node, subg);
                modRefAdd(si.modref.mayref, node->getUses(), node, subg);
            }
        }
    }
    DBG_SECTION_END(dda, "Computing modref for subgraph " << subg->getName() << " done");
}

void MemorySSATransformation::findAllDefinitionsFromCall(Definitions& D,
                                                         RWNodeCall *C) {
    if (D.isProcessed()) {
        return;
    }

    DBG_SECTION_BEGIN(dda, "Finding all definitions for a call " << C);

    for (auto& callee : C->getCallees()) {
        auto *subg = callee.getSubgraph();
        if (!subg) {
            auto *called = callee.getCalledValue();
            for (auto& ds : called->getDefines()) {
                findDefinitionsFromCall(D, C, ds);
            }
            for (auto& ds : called->getOverwrites()) {
                findDefinitionsFromCall(D, C, ds);
            }
        } else {
            auto& si = getSubgraphInfo(subg);
            computeModRef(subg, si);
            assert(si.modref.isInitialized());

            for (auto& it : si.modref.maydef) {
                if (it.first->isUnknown()) {
                    for (auto& it2 : it.second) {
                        D.unknownWrites.insert(D.unknownWrites.end(),
                                               it2.second.begin(),
                                               it2.second.end());
                    }
                    continue;
                }
                for (auto& it2: it.second) {
                    findDefinitionsFromCall(D, C, {it.first,
                                                   it2.first.start,
                                                   it2.first.length()});
                }
            }
        }
    }

    DBG_SECTION_BEGIN(dda, "Finding all definitions for a call " << C << " finished");
    D.setProcessed();
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
    // in which case we want to process it again
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
            // assign 'kills' to not to search for what we already have
            DefinitionsMap<RWNode> tmpDefs = D.kills;
            collectAllDefinitions(tmpDefs, *I, visitedBlocks);
            defs.add(tmpDefs);
            // NOTE: we cannot cache here because of the DFS nature of the search
            // (the found definitions does not contain _all_ reaching definitions
            //  as we search only for those that we do not have already)
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
