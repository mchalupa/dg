#include <set>
#include <vector>

#include "dg/ADT/Bitvector.h"
#include "dg/MemorySSA/MemorySSA.h"
//#include "dg/BBlocksBuilder.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

/// ------------------------------------------------------------------
// class MemorySSATransformation
/// ------------------------------------------------------------------

// find definitions of a given node
std::vector<RWNode *> MemorySSATransformation::findDefinitions(RWNode *node) {
    DBG_SECTION_BEGIN(dda, "Searching definitions for node " << node->getID());

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

    for (const auto &ds : node->getUses()) {
        assert(ds.target && "Target is null");

        // add the definitions from the beginning of this block to the defs
        // container
        auto defSet = D.get(ds);
        assert((!defSet.empty() || D.unknownWrites.empty()) &&
               "BUG: if we found no definitions, also unknown writes must be "
               "empty");
        defs.insert(defs.end(), defSet.begin(), defSet.end());

        addUncoveredFromPredecessors(block, D, ds, defs);
    }

    DBG_SECTION_END(dda,
                    "Done searching definitions for node " << node->getID());
    return defs;
}

// find definitions of a given node
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWNode *node, const DefSite &ds) {
    DBG(dda, "Searching definitions for node " << node->getID());

    // we use this only for PHI nodes
    assert(ds.target && "Target is null");
    assert(!ds.target->isUnknown() && "Searching unknown memory");

    auto *block = node->getBBlock();
    assert(block && "Need bblock");

    // gather all definitions from the beginning of the block
    // to the node (we must do that always, because adding PHI
    // nodes changes the definitions)
    auto D = findDefinitionsInBlock(node);
    std::vector<RWNode *> defs;

    // add the definitions from the beginning of this block to the defs
    // container
    auto defSet = D.get(ds);
    assert((!defSet.empty() || D.unknownWrites.empty()) &&
           "BUG: if we found no definitions, also unknown writes must be "
           "empty");
    defs.insert(defs.end(), defSet.begin(), defSet.end());

    addUncoveredFromPredecessors(block, D, ds, defs);

    return defs;
}

RWNode *MemorySSATransformation::createPhi(const DefSite &ds, RWNodeType type) {
    // This phi is the definition that we are looking for.
    _phis.emplace_back(&graph.create(type));
    auto *phi = _phis.back();
    assert(phi->isPhi() && "Got wrong type");

    phi->addOverwrites(ds);

    DBG(dda, "Created PHI with ID " << phi->getID());
    return phi;
}

RWNode *MemorySSATransformation::createPhi(Definitions &D, const DefSite &ds,
                                           RWNodeType type) {
    auto *phi = createPhi(ds, type);

    // update definitions in the block -- this
    // phi node defines previously uncovered memory
    auto uncovered = D.uncovered(ds);
    for (auto &interval : uncovered) {
        DefSite uds{ds.target, interval.start, interval.length()};
        // NOTE: add, not update! D can already have some weak definitions
        // of this memory (but they are "uncovered" as they are only weak
        D.definitions.add(uds, phi);
        assert(D.kills.get(uds).empty() &&
               "BUG: Basic block already kills this memory");
        D.kills.add(uds, phi);

        // to simulate the whole LVN, we must add also writes to unknown memory
        if (!D.getUnknownWrites().empty()) {
            D.definitions.add(uds, D.getUnknownWrites());
        }
    }

    return phi;
}

RWNode *MemorySSATransformation::createAndPlacePhi(RWBBlock *block,
                                                   const DefSite &ds) {
    // create PHI node and find definitions for the PHI node
    auto &D = getBBlockDefinitions(block, &ds);
    auto *phi = createPhi(D, ds);
    block->prepend(phi);
    return phi;
}

static inline bool canBeInput(const RWNode *node, RWSubgraph *subg) {
    // can escape or already escaped
    if (node->canEscape())
        return true;
    if (node->getBBlock())
        return node->getBBlock()->getSubgraph() != subg;
    return false;
}

void MemorySSATransformation::findDefinitionsInMultiplePredecessors(
        RWBBlock *block, const DefSite &ds, std::vector<RWNode *> &defs) {
    RWNode *phi = nullptr;
    if (block->hasPredecessors()) {
        // The phi node will be placed at the beginning of the block,
        // so the iterator should not be invalidated.
        phi = createAndPlacePhi(block, ds);
        // recursively find definitions for this phi node
        findPhiDefinitions(phi);
    } else if (canBeInput(ds.target, block->getSubgraph())) {
        // this is the entry block, so we add a PHI node
        // representing "input" into this procedure
        // (but only if the input can be used from the called procedure)
        phi = createPhi(getBBlockDefinitions(block, &ds), ds,
                        /* type = */ RWNodeType::INARG);
        auto *subg = block->getSubgraph();
        auto &summary = getSubgraphSummary(subg);
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
                                                       const DefSite &ds) {
    assert(block);
    assert(ds.target && "Target is null");
    assert(!ds.target->isUnknown() &&
           "Finding uknown memory"); // this is handled differently

    std::vector<RWNode *> defs;

    // if we have a unique predecessor,
    // we can find the definitions there and continue searching in the
    // predecessor if something is missing
    if (auto *pred = block->getSinglePredecessor()) {
        auto pdefs = findDefinitions(pred, ds);
#ifndef NDEBUG
        auto &D = getBBlockDefinitions(pred, &ds);
        assert((!pdefs.empty() || D.unknownWrites.empty()) &&
               "BUG: if we found no definitions, also unknown writes must be "
               "empty");
#endif // not NDEBUG
        defs.insert(defs.end(), pdefs.begin(), pdefs.end());
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
    auto *block = phi->getBBlock();

    assert(block);
    assert(!block->getSinglePredecessor() &&
           "Phi in a block with single predecessor");

    findPhiDefinitions(phi, block->predecessors());
}

void MemorySSATransformation::addUncoveredFromPredecessors(
        RWBBlock *block, Definitions &D, const DefSite &ds,
        std::vector<RWNode *> &defs) {
    auto uncovered = D.uncovered(ds);
    for (auto &interval : uncovered) {
        auto preddefs = findDefinitionsInPredecessors(
                block, {ds.target, interval.start, interval.length()});
        defs.insert(defs.end(), preddefs.begin(), preddefs.end());
    }
}

///
// Find the nodes that define the given def-site in this block
// (using the definitions computed for each single block)
// Create PHI nodes if needed.
std::vector<RWNode *>
MemorySSATransformation::findDefinitions(RWBBlock *block, const DefSite &ds) {
    assert(ds.target && "Target is null");
    assert(block && "Block is null");

    // Find known definitions.
    auto &D = getBBlockDefinitions(block, &ds);
    auto defSet = D.get(ds);
    assert((!defSet.empty() || D.unknownWrites.empty()) &&
           "BUG: if we found no definitions, also unknown writes must be "
           "empty");
    std::vector<RWNode *> defs(defSet.begin(), defSet.end());

    addUncoveredFromPredecessors(block, D, ds, defs);

    return defs;
}

bool MemorySSATransformation::callMayDefineTarget(RWNodeCall *C,
                                                  RWNode *target) {
    // check if this call may define the memory at all
    for (auto &callee : C->getCallees()) {
        auto *subg = callee.getSubgraph();
        if (!subg) {
            auto *cv = callee.getCalledValue();
            if (cv->defines(target)) {
                return true;
            }
        } else {
            auto &si = getSubgraphInfo(subg);
            computeModRef(subg, si);
            assert(si.modref.isInitialized());
            if (si.modref.mayDefineOrUnknown(target)) {
                return true;
            }
        }
    }

    return false;
}

void MemorySSATransformation::findDefinitionsInSubgraph(RWNode *phi,
                                                        RWNodeCall *C,
                                                        const DefSite &ds,
                                                        RWSubgraph *subg) {
    DBG_SECTION_BEGIN(tmp,
                      "Searching definitions in subgraph " << subg->getName());
    auto &summary = getSubgraphSummary(subg);
    auto &si = getSubgraphInfo(subg);
    computeModRef(subg, si);
    assert(si.modref.isInitialized());

    // Add the definitions that we have found in previous exploration
    phi->addDefUse(summary.getOutputs(ds));

    // search the definitions that we have not found yet
    for (auto &subginterval : summary.getUncoveredOutputs(ds)) {
        // we must create a new phi for each subgraph inside the subgraph
        // (these phis will be merged by the single 'phi').
        // Of course, we create them only when not already present.
        auto subgds =
                DefSite{ds.target, subginterval.start, subginterval.length()};

        // do not search the procedure if it cannot define the memory
        // (this saves creating PHI nodes). If it may define only
        // unknown memory, add that definitions directly and continue searching
        // before the call.
        if (!si.modref.mayDefine(ds.target)) {
            if (si.modref.mayDefineUnknown()) {
                auto *subgphi =
                        createPhi(subgds, /* type = */ RWNodeType::OUTARG);
                summary.addOutput(subgds, subgphi);
                for (const auto &it : si.modref.getMayDef(UNKNOWN_MEMORY)) {
                    subgphi->addDefUse(it);
                }
                phi->addDefUse(subgphi);
            }
            // continue the search before the call
            phi->addDefUse(findDefinitions(C, subgds));
            continue;
        }

        auto *subgphi = createPhi(subgds, /* type = */ RWNodeType::OUTARG);
        summary.addOutput(subgds, subgphi);
        phi->addDefUse(subgphi);

        // find the new phi operands
        for (auto *subgblock : subg->bblocks()) {
            if (subgblock->hasSuccessors()) {
                continue;
            }
            if (!subgblock->isReturnBBlock()) {
                // ignore blocks that does not return to this subgraph
                continue;
            }
            subgphi->addDefUse(findDefinitions(subgblock, ds));
        }
    }
    DBG_SECTION_END(tmp, "Done searching definitions in subgraph "
                                 << subg->getName());
}

void MemorySSATransformation::addDefinitionsFromCalledValue(
        RWNode *phi, RWNodeCall *C, const DefSite &ds, RWNode *calledValue) {
    std::vector<RWNode *> defs;
    Definitions D;
    // FIXME: cache this somehow ?
    D.update(calledValue);

    auto defSet = D.get(ds);
    assert((!defSet.empty() || D.unknownWrites.empty()) &&
           "BUG: if we found no definitions, also unknown writes must be "
           "empty");
    defs.insert(defs.end(), defSet.begin(), defSet.end());
    addUncoveredFromPredecessors(C->getBBlock(), D, ds, defs);
    phi->addDefUse(defs);
}

void MemorySSATransformation::fillDefinitionsFromCall(Definitions &D,
                                                      RWNodeCall *C,
                                                      const DefSite &ds) {
    if (!callMayDefineTarget(C, ds.target))
        return;

    // find the uncovered parts of the sought definition
    auto uncovered = D.uncovered(ds);
    for (auto &interval : uncovered) {
        auto uncoveredds =
                DefSite{ds.target, interval.start, interval.length()};
        // this phi will merge the definitions from all the
        // possibly called subgraphs
        auto *phi = createPhi(D, uncoveredds, RWNodeType::CALLOUT);
        C->getBBlock()->append(phi);
        C->addOutput(phi);

        // recursively find definitions for this phi node
        for (auto &callee : C->getCallees()) {
            if (auto *subg = callee.getSubgraph()) {
                findDefinitionsInSubgraph(phi, C, uncoveredds, subg);
            } else {
                addDefinitionsFromCalledValue(phi, C, uncoveredds,
                                              callee.getCalledValue());
            }
        }
    }
}

// get all callers of the function and find the given definitions reaching these
// call-sites
void MemorySSATransformation::findDefinitionsFromCalledFun(RWNode *phi,
                                                           RWSubgraph *subg,
                                                           const DefSite &ds) {
    for (auto *callsite : subg->getCallers()) {
        auto *C = RWNodeCall::get(callsite);
        assert(C && "Callsite is not a call");

        auto *bblock = callsite->getBBlock();
        assert(bblock && getBBlockInfo(bblock).isCallBlock());

        // create input PHI for this call
        auto *callphi = createPhi(ds, RWNodeType::CALLIN);
        bblock->insertBefore(callphi, C);
        C->addInput(callphi);

        phi->addDefUse(callphi);
        callphi->addDefUse(findDefinitions(callphi, ds));
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
Definitions &MemorySSATransformation::getBBlockDefinitions(RWBBlock *b,
                                                           const DefSite *ds) {
    auto &bi = getBBlockInfo(b);
    auto &D = bi.getDefinitions();

    if (D.isProcessed()) {
        DBG(dda, "Retrived processed definitions for block " << b->getID());
        return D;
    }

    if (bi.isCallBlock()) {
        if (ds) {
            fillDefinitionsFromCall(D, bi.getCall(), *ds);
            DBG(dda,
                "Retrived (partial) definitions for call block " << b->getID());
        } else {
            DBG(dda, "Filling all definitions for call block " << b->getID());
            fillDefinitionsFromCall(D, bi.getCall());
            assert(D.isProcessed());
        }
    } else {
        performLvn(D, b); // normal basic block
        assert(D.isProcessed());
        DBG(dda, "Retrived LVN'd definitions for block " << b->getID());
    }
    return D;
}

// perform Lvn for one block
void MemorySSATransformation::performLvn(Definitions &D, RWBBlock *block) {
    DBG_SECTION_BEGIN(dda, "Starting LVN for block " << block->getID());

    assert(!D.isProcessed() && "Processing a block multiple times");

    for (RWNode *node : block->getNodes()) {
        D.update(node);
    }

    D.setProcessed();
    DBG_SECTION_END(dda, "LVN of block " << block->getID() << " finished");
}

///
// The same as performLVN() but only up to some point (and returns the map).
// Also, if mem is specified, then search for effects only to this memory.
Definitions MemorySSATransformation::findDefinitionsInBlock(RWNode *to,
                                                            const RWNode *mem) {
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

Definitions
MemorySSATransformation::findEscapingDefinitionsInBlock(RWNode *to) {
    auto *block = to->getBBlock();
    // perform LVN up to the node
    Definitions D;
    for (RWNode *node : block->getNodes()) {
        if (node == to)
            break;
        if (node->canEscape()) {
            D.update(node);
        }
    }

    return D;
}

///
/// Copy definitions from 'from' map to 'to' map.
/// Copy only those that are not already killed by 'to' map
/// (thus simulating the state when 'to' is executed after 'from')
///
static void joinDefinitions(DefinitionsMap<RWNode> &from,
                            DefinitionsMap<RWNode> &to, bool escaping = false) {
    for (const auto &it : from) {
        if (escaping && !it.first->canEscape()) {
            continue;
        }

        if (!to.definesTarget(it.first)) {
            // just copy the definitions
            to.add(it.first, it.second);
            continue;
        }

        for (const auto &it2 : it.second) {
            const auto &interv = it2.first;
            auto uncovered = to.undefinedIntervals(
                    {it.first, interv.start, interv.length()});
            for (auto &undefInterv : uncovered) {
                // we still do not have definitions for these bytes, add it
                to.add({it.first, undefInterv.start, undefInterv.length()},
                       it2.second);
            }
        }
    }
}

static void joinDefinitions(Definitions &from, Definitions &to,
                            bool escaping = false) {
    joinDefinitions(from.definitions, to.definitions, escaping);
    to.unknownWrites.insert(to.unknownWrites.end(), from.unknownWrites.begin(),
                            from.unknownWrites.end());
    // we ignore 'kills' and 'unknownReads' as this function is used
    // only when searching for all definitions
}

void MemorySSATransformation::fillDefinitionsFromCall(Definitions &D,
                                                      RWNodeCall *C) {
    if (D.isProcessed()) {
        return;
    }

    DBG_SECTION_BEGIN(dda, "Finding all definitions for a call " << C);

    for (auto &callee : C->getCallees()) {
        auto *subg = callee.getSubgraph();
        if (!subg) {
            auto *called = callee.getCalledValue();
            for (const auto &ds : called->getDefines()) {
                fillDefinitionsFromCall(D, C, ds);
            }
            for (const auto &ds : called->getOverwrites()) {
                fillDefinitionsFromCall(D, C, ds);
            }
        } else {
            auto &si = getSubgraphInfo(subg);
            computeModRef(subg, si);
            assert(si.modref.isInitialized());

            for (const auto &it : si.modref.maydef) {
                if (it.first->isUnknown()) {
                    for (const auto &it2 : it.second) {
                        D.unknownWrites.insert(D.unknownWrites.end(),
                                               it2.second.begin(),
                                               it2.second.end());
                    }
                    continue;
                }
                for (const auto &it2 : it.second) {
                    fillDefinitionsFromCall(
                            D, C,
                            {it.first, it2.first.start, it2.first.length()});
                }
            }
        }
    }

    DBG_SECTION_BEGIN(dda, "Finding all definitions for a call "
                                   << C << " finished");
    D.setProcessed();
}

void MemorySSATransformation::collectAllDefinitions(
        Definitions &defs, RWBBlock *from, std::set<RWBBlock *> &visitedBlocks,
        bool escaping) {
    assert(from);

    if (!visitedBlocks.insert(from).second) {
        return; // we already visited this block
    }

    // get the definitions from this block
    joinDefinitions(getBBlockDefinitions(from), defs, escaping);

    // recur into predecessors
    if (auto *singlePred = from->getSinglePredecessor()) {
        collectAllDefinitions(defs, singlePred, visitedBlocks, escaping);
    } else {
        auto olddefinitions = defs.definitions;
        for (auto I = from->pred_begin(), E = from->pred_end(); I != E; ++I) {
            Definitions tmpDefs;
            tmpDefs.definitions = olddefinitions;
            collectAllDefinitions(tmpDefs, *I, visitedBlocks, escaping);
            defs.definitions.add(tmpDefs.definitions);
            defs.unknownWrites.insert(defs.unknownWrites.end(),
                                      tmpDefs.unknownWrites.begin(),
                                      tmpDefs.unknownWrites.end());
        }
    }
}

Definitions MemorySSATransformation::collectAllDefinitions(RWNode *from) {
    Definitions defs; // auxiliary map for finding defintions
    collectAllDefinitions(from, defs);
    return defs;
}

void MemorySSATransformation::collectAllDefinitionsInCallers(Definitions &defs,
                                                             RWSubgraph *subg) {
    auto &callers = subg->getCallers();
    if (callers.empty()) {
        return;
    }

    // create an input PHI node
    auto &summary = getSubgraphSummary(subg);
    DefSite ds{UNKNOWN_MEMORY};
    RWNode *phi = summary.getUnknownPhi();
    if (!phi) {
        phi = createPhi(ds, /* type = */ RWNodeType::INARG);
        summary.addInput(ds, phi);
    }

    defs.unknownWrites.push_back(phi);

    for (auto *callsite : subg->getCallers()) {
        // create input PHI for this call
        auto *C = RWNodeCall::get(callsite);
        auto *callphi = C->getUnknownPhi();
        if (callphi) {
            phi->addDefUse(callphi);
            continue;
        }

        callphi = createPhi(ds, RWNodeType::CALLIN);
        C->addUnknownInput(callphi);
        phi->addDefUse(callphi);

        // NOTE: search _all_ definitions, not only those that are
        // not covered by 'defs'. Therefore, we can reuse this search
        // in all later searches.
        // auto tmpDefs = defs;
        Definitions tmpDefs;
        collectAllDefinitions(callsite, tmpDefs, /* escaping = */ true);
        for (const auto &it : tmpDefs.definitions) {
            for (const auto &it2 : it.second) {
                callphi->addDefUse(it2.second);
            }
        }
        callphi->addDefUse(tmpDefs.unknownWrites);
    }
}

void MemorySSATransformation::collectAllDefinitions(RWNode *from,
                                                    Definitions &defs,
                                                    bool escaping) {
    assert(from->getBBlock() && "The node has no BBlock");

    auto *block = from->getBBlock();
    std::set<RWBBlock *> visitedBlocks; // for terminating the search

    Definitions D;
    if (escaping) {
        D = findEscapingDefinitionsInBlock(from);
    } else {
        D = findDefinitionsInBlock(from);
    }

    ///
    // -- Get the definitions from predecessors in this subgraph --
    //
    // NOTE: do not add block to visitedBlocks, it may be its own predecessor,
    // in which case we want to process it again
    if (auto *singlePred = block->getSinglePredecessor()) {
        collectAllDefinitions(defs, singlePred, visitedBlocks, escaping);
    } else {
        // multiple predecessors
        for (auto I = block->pred_begin(), E = block->pred_end(); I != E; ++I) {
            // assign 'kills' to not to search for what we already have
            Definitions tmpDefs;
            tmpDefs.definitions = D.kills;
            collectAllDefinitions(tmpDefs, *I, visitedBlocks, escaping);
            defs.definitions.add(tmpDefs.definitions);
            defs.unknownWrites.insert(defs.unknownWrites.end(),
                                      tmpDefs.unknownWrites.begin(),
                                      tmpDefs.unknownWrites.end());
        }
    }

    ///
    // -- Get the definitions from predecessors
    //    in the callers of this subgraph --
    //
    collectAllDefinitionsInCallers(defs, block->getSubgraph());

    // create the final map of definitions reaching the 'from' node
    joinDefinitions(defs, D);
    defs.swap(D);
}

std::vector<RWNode *>
MemorySSATransformation::findAllDefinitions(RWNode *from) {
    DBG_SECTION_BEGIN(dda, "MemorySSA - finding all definitions for node "
                                   << from->getID());

    auto defs = collectAllDefinitions(from);

    DBG_SECTION_END(dda, "MemorySSA - finding all definitions for node "
                                 << from->getID() << " done");

    auto values = defs.definitions.values();
    values.insert(defs.unknownWrites.begin(), defs.unknownWrites.end());
    return std::vector<RWNode *>(values.begin(), values.end());
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
        auto &si = _subgraphs_info[subg];
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

RWNode *MemorySSATransformation::insertUse(RWNode *where, RWNode *mem,
                                           const Offset &off,
                                           const Offset &len) {
    // DBG_SECTION_BEGIN(dda, "Adding MU node");
    auto &use = graph.create(RWNodeType::MU);
    use.addUse({mem, off, len});
    use.insertBefore(where);
    where->getBBlock()->insertBefore(&use, where);
    // DBG_SECTION_END(dda, "Created MU node " << use->getID());

    return &use;
}

static void recGatherNonPhisDefs(RWNode *phi,
                                 dg::ADT::SparseBitvectorHashImpl &phis,
                                 dg::ADT::SparseBitvectorHashImpl &ret,
                                 bool intraproc = false) {
    assert(phi->isPhi());
    // set returns the previous value, so if its 'true',
    // we already had the phi
    if (phis.set(phi->getID()))
        return; // we already visited this phi

    for (auto *n : phi->defuse) {
        if (!n->isPhi()) {
            ret.set(n->getID());
        } else {
            if (intraproc && n->isInOut()) {
                ret.set(n->getID());
            } else {
                recGatherNonPhisDefs(n, phis, ret, intraproc);
            }
        }
    }
}

// recursivelu replace all phi values with its non-phi definitions
template <typename ContT>
std::vector<RWNode *> gatherNonPhisDefs(ReadWriteGraph *graph,
                                        const ContT &nodes,
                                        bool intraproc = false) {
    dg::ADT::SparseBitvectorHashImpl ret; // use set to get rid of duplicates
    dg::ADT::SparseBitvectorHashImpl
            phis; // set of visited phi nodes - to check the fixpoint

    for (auto n : nodes) {
        if (!n->isPhi()) {
            assert(n->getID() > 0);
            ret.set(n->getID());
        } else {
            if (intraproc && n->isInOut()) {
                assert(n->getID() > 0);
                ret.set(n->getID());
            } else {
                recGatherNonPhisDefs(n, phis, ret, intraproc);
            }
        }
    }

    std::vector<RWNode *> retval;
    retval.reserve(ret.size());
    for (auto i : ret) {
        retval.push_back(graph->getNode(i));
    }
    return retval;
}

std::vector<RWNode *> MemorySSATransformation::getDefinitions(RWNode *use) {
    // on demand triggering finding the definitions
    if (!use->defuse.initialized()) {
        use->addDefUse(findDefinitions(use));
        assert(use->defuse.initialized());
    }
    return gatherNonPhisDefs(getGraph(), use->defuse);
}

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RWNode *>
MemorySSATransformation::getDefinitions(RWNode *where, RWNode *mem,
                                        const Offset &off, const Offset &len) {
    auto *use = insertUse(where, mem, off, len);
    return getDefinitions(use);
}

void MemorySSATransformation::run() {
    DBG_SECTION_BEGIN(dda, "Initializing MemorySSA analysis");

    initialize();

    // the rest is on-demand :)

    DBG_SECTION_END(dda, "Initializing MemorySSA analysis finished");
}

} // namespace dda
} // namespace dg
