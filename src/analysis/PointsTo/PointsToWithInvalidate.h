#ifndef _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_
#define _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_

#include <cassert>

#include "PointsToFlowSensitive.h"

namespace dg {
namespace analysis {
namespace pta {

class PointsToWithInvalidate : public PointsToFlowSensitive
{
    static bool canChangeMM(PSNode *n) {
        if (n->getType() == PSNodeType::FREE ||
            n->getType() == PSNodeType::INVALIDATE_LOCALS)
            return true;

        return PointsToFlowSensitive::canChangeMM(n);
    }

    static bool needsMerge(PSNode *n) {
        return n->predecessorsNum() > 1 || canChangeMM(n);
    }

public:
    using MemoryMapT = PointsToFlowSensitive::MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointsToWithInvalidate(PointerSubgraph *ps)
    : PointsToFlowSensitive(ps) {}

    bool afterProcessed(PSNode *n) override
    {
        bool changed = false;
        PointsToSetT *strong_update = nullptr;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        // we must have the memory map, we created it
        // in the beforeProcessed method
        assert(mm && "Do not have memory map");

        // every store is a strong update
        // FIXME: memcpy can be strong update too
        if (n->getType() == PSNodeType::STORE)
            strong_update = &n->getOperand(1)->pointsTo;
        else if (n->getType() == PSNodeType::FREE)
            strong_update = &n->getOperand(0)->pointsTo;
        else if (n->getType() == PSNodeType::INVALIDATE_LOCALS)
            strong_update = &n->pointsTo;

        // merge information from predecessors if there's
        // more of them (if there's just one predecessor
        // and this is not a store, the memory map couldn't
        // change, so we don't have to do that)
        if (needsMerge(n)) {
            for (PSNode *p : n->getPredecessors()) {
                MemoryMapT *pm = p->getData<MemoryMapT>();
                // merge pm to mm (but only if pm was already created)
                if (pm) {
                    changed |= mergeMaps(mm, pm, strong_update);
                }
            }
        }

        return changed;
    }

    void getMemoryObjectsPointingTo(PSNode *where, const Pointer& pointer,
                          std::vector<MemoryObject *>& objects) override
    {
        MemoryMapT *mm = where->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        for (auto& I : *mm) {
            MemoryObject *mo = I.second.get();
            for (auto& it : mo->pointsTo) {
                for (const auto& ptr : it.second) {
                    if (ptr.target == pointer.target) {
                        objects.push_back(mo);
                        break;
                    }
                }
            }
        }
    }
    
    void getLocalMemoryObjects(PSNode *where, 
                          std::vector<MemoryObject *>& objects) override
    {
        MemoryMapT *mm = where->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        for (auto& I : *mm) {
            MemoryObject *mo = I.second.get();
            for (auto& it : mo->pointsTo) {
                for (const auto& ptr : it.second) {
                    if (!ptr.target->isHeap() &&
                        !ptr.target->isGlobal() &&
                        ptr.target->getParent() == where->getParent()) {
                        objects.push_back(mo);
                        break;
                    }
                }
            }
        }
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_

