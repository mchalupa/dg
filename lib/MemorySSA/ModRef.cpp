#include "dg/MemorySSA/MemorySSA.h"
#include "dg/util/debug.h"

namespace dg {
namespace dda {

static inline bool canBeOutput(const RWNode *node, RWSubgraph *subg) {
    // can escape or already escaped
    return node->canEscape() ||
           (!node->getBBlock() || node->getBBlock()->getSubgraph() != subg);
}

template <typename MR, typename C>
static void modRefAdd(MR &modref, const C &c, RWNode *node, RWSubgraph *subg) {
    assert(node && "Node the definion node");
    for (const DefSite &ds : c) {
        // can escape
        if (canBeOutput(ds.target, subg)) {
            modref.add(ds, node);
        }
    }
}

void MemorySSATransformation::computeModRef(RWSubgraph *subg,
                                            SubgraphInfo &si) {
    if (si.modref.isInitialized()) {
        return;
    }

    DBG_SECTION_BEGIN(dda, "Computing modref for subgraph " << subg->getName());

    // set it here due to recursive procedures
    si.modref.setInitialized();

    // iterate over the blocks (note: not over the infos, those
    // may not be created if the block was not used yet
    for (auto *b : subg->bblocks()) {
        auto &bi = si.getBBlockInfo(b);
        if (bi.isCallBlock()) {
            auto *C = bi.getCall();
            for (auto &callee : C->getCallees()) {
                auto *csubg = callee.getSubgraph();
                if (csubg) {
                    auto &callsi = getSubgraphInfo(csubg);
                    computeModRef(csubg, callsi);
                    assert(callsi.modref.isInitialized());

                    si.modref.add(callsi.modref);
                } else {
                    // undefined function
                    modRefAdd(si.modref.maydef,
                              callee.getCalledValue()->getDefines(), C, csubg);
                    modRefAdd(si.modref.maydef,
                              callee.getCalledValue()->getOverwrites(), C,
                              csubg);
                    modRefAdd(si.modref.mayref,
                              callee.getCalledValue()->getUses(), C, csubg);
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
    DBG_SECTION_END(dda, "Computing modref for subgraph " << subg->getName()
                                                          << " done");
}

} // namespace dda
} // namespace dg
