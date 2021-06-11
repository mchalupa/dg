#ifndef DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_
#define DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_

#include <cassert>
#include <memory>
#include <vector>

#include "PointerAnalysis.h"

namespace dg {
namespace pta {

///
// Flow-insensitive inclusion-based pointer analysis
//
class PointerAnalysisFI : public PointerAnalysis {
    std::vector<std::unique_ptr<MemoryObject>> memory_objects;

    void preprocessGEPs() {
        // if a node is in a loop (a scc that has more than one node),
        // then every GEP that is also stored to the same memory afterwards
        // in the loop will end up with Offset::UNKNOWN after some
        // number of iterations (in FI analysis), so we can do that right now
        // and save iterations

        assert(getPG() && "Must have PG");
        for (const auto &sg : getPG()->getSubgraphs()) {
            for (const auto &loop : sg->getLoops()) {
                for (PSNode *n : loop) {
                    if (PSNodeGep *gep = PSNodeGep::get(n))
                        gep->setOffset(Offset::UNKNOWN);
                }
            }
        }
    }

  public:
    PointerAnalysisFI(PointerGraph *ps) : PointerAnalysisFI(ps, {}) {}

    PointerAnalysisFI(PointerGraph *ps, const PointerAnalysisOptions &opts)
            : PointerAnalysis(ps, opts) {
        memory_objects.reserve(
                std::max(ps->size() / 100, static_cast<size_t>(8)));
    }

    void preprocess() override {
        if (options.preprocessGeps)
            preprocessGEPs();
    }

    void getMemoryObjects(PSNode *where, const Pointer &pointer,
                          std::vector<MemoryObject *> &objects) override {
        // irrelevant in flow-insensitive
        (void) where;
        PSNode *n = pointer.target;

        // we want to have memory in allocation sites
        if (n->getType() == PSNodeType::CAST || n->getType() == PSNodeType::GEP)
            n = n->getOperand(0);
        else if (n->getType() == PSNodeType::CONSTANT) {
            assert(n->pointsTo.size() == 1);
            n = (*n->pointsTo.begin()).target;
        }

        if (n->getType() == PSNodeType::FUNCTION)
            return;

        assert(n->getType() == PSNodeType::ALLOC ||
               n->getType() == PSNodeType::UNKNOWN_MEM);

        MemoryObject *mo = n->getData<MemoryObject>();
        if (!mo) {
            mo = new MemoryObject(n);
            memory_objects.emplace_back(mo);
            n->setData<MemoryObject>(mo);
        }

        objects.push_back(mo);
    }
};

} // namespace pta
} // namespace dg

#endif // DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_
