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

    bool beforeProcessed(PSNode *n) override
    {
        MemoryMapT *mm = n->getData<MemoryMapT>();
        if (mm)
            return false;

        // on these nodes the memory map can change
        if (needsMerge(n)) { // root node
            mm = createMM();
        } else {
            // this node can not change the memory map,
            // so just add a pointer from the predecessor
            // to this map
            PSNode *pred = n->getSinglePredecessor();
            mm = pred->getData<MemoryMapT>();
            assert(mm && "No memory map in the predecessor");
        }

        assert(mm && "Did not create the MM");

        // memory map initialized, set it as data,
        // so that we won't initialize it again
        n->setData<MemoryMapT>(mm);

        return true;
    }

    bool afterProcessed(PSNode *n) override
    {
        bool changed = false;

        if (n->getType() == PSNodeType::INVALIDATE_LOCALS) {
            changed |= handleInvalidateLocals(n);
        } else if (n->getType() == PSNodeType::FREE) {
            changed |= handleFree(n);
        }

        PointsToSetT *strong_update = nullptr;
        // every store is a strong update
        // FIXME: memcpy can be strong update too
        if (n->getType() == PSNodeType::STORE)
            strong_update = &n->getOperand(1)->pointsTo;
        else if (n->getType() == PSNodeType::FREE)
            strong_update = &n->getOperand(0)->pointsTo;
        else if (n->getType() == PSNodeType::INVALIDATE_LOCALS)
            strong_update = &n->pointsTo;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        assert(mm && "Do not have memory map");

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

    static bool isLocal(PSNodeAlloc *alloc, PSNode *where) {
        return !alloc->isHeap() && !alloc->isGlobal() &&
                alloc->getParent() == where->getParent();
    }

    static bool containsLocal(PSNode *where, PointsToSetT& S) {
        for (const auto& ptr : S) {
            if (ptr.isNull() || ptr.isUnknown() || ptr.isInvalidated())
                continue;

            if (PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target)) {
                if (isLocal(alloc, where))
                    return true;
            }
        }

        return false;
    }

    // not very efficient
    static void replaceLocalWithInv(PSNode *where, PointsToSetT& S1) {
        PointsToSetT S;

        for (const auto& ptr : S1) {
            if (ptr.isNull() || ptr.isUnknown() || ptr.isInvalidated())
                continue;

            if (PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target)) {
                if (!isLocal(alloc, where))
                    S.insert(ptr);
            }
        }

        S.insert(INVALIDATED);
        S1.swap(S);
    }

    bool handleInvalidateLocals(PSNode *node)
    {
        bool changed = false;
        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        for (auto& I : *mm) {
            if (I.first == INVALIDATED ||
                I.first == UNKNOWN_MEMORY ||
                I.first == NULLPTR)
                continue;

            MemoryObject *mo = I.second.get();
            for (auto& it : mo->pointsTo) {
                if (containsLocal(node, it.second)) {
                    replaceLocalWithInv(node, it.second);
                    // perform a strong update on this MO
                    // XXX: do not use addPointsTo, because it could
                    // merge together concrete offsets to unnown offset,
                    // we need here all the offsets
                    node->pointsTo.insert({I.first, it.first});
                    changed = true;
                }
            }
        }

        return changed;
    }

    static bool pointsToTarget(PointsToSetT& S, PSNode *target) {
        for (const auto& ptr : S) {
            if (ptr.target == target) {
                return true;
            }
        }

        return false;
    }

    static void replaceTargetWithInv(PointsToSetT& S1, PSNode *target) {
        PointsToSetT S;
        for (const auto& ptr : S1) {
            if (ptr.target != target)
                S.insert(ptr);
        }

        S.insert(INVALIDATED);
        S.swap(S1);
    }

    bool handleFree(PSNode *node)
    {
        bool changed = false;

        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        PSNode *operand = node->getOperand(0);

        for (auto& I : *mm) {
            if (I.first == INVALIDATED ||
                I.first == UNKNOWN_MEMORY ||
                I.first == NULLPTR)
                continue;

            MemoryObject *mo = I.second.get();
            for (auto& it : mo->pointsTo) {
                for (const auto& ptr : operand->pointsTo) {
                    if (pointsToTarget(it.second, ptr.target)) {
                        replaceTargetWithInv(it.second, ptr.target);

                        // XXX: do not use addPointsTo, because it could
                        // merge together concrete offsets to unnown offset,
                        // we need here all the offsets
                        // FIXME: store the information about strong update
                        // somewhere else that in pointsTo set
                        // as it is not a points-to set...
                        node->pointsTo.insert({I.first, it.first});
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_

