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
            n->getType() == PSNodeType::INVALIDATE_OBJECT ||
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
    : PointsToFlowSensitive(ps, true) {}

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

        if (n->getType() == PSNodeType::INVALIDATE_LOCALS)
            return handleInvalidateLocals(n);
        if (n->getType() == PSNodeType::INVALIDATE_OBJECT)
            return invalidateMemory(n);
        if (n->getType() == PSNodeType::FREE)
            return invalidateMemory(n);

        assert(n->getType() != PSNodeType::FREE &&
               n->getType() != PSNodeType::INVALIDATE_OBJECT &&
               n->getType() != PSNodeType::INVALIDATE_LOCALS);

        PointsToSetT *strong_update = nullptr;
        // every store is a strong update
        // FIXME: memcpy can be strong update too
        if (n->getType() == PSNodeType::STORE)
            strong_update = &n->getOperand(1)->pointsTo;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        assert(mm && "Do not have memory map");

        // merge information from predecessors if there's
        // more of them (if there's just one predecessor
        // and this is not a store, the memory map couldn't
        // change, so we don't have to do that)
        if (needsMerge(n)) {
            for (PSNode *p : n->getPredecessors()) {
                if (MemoryMapT *pm = p->getData<MemoryMapT>()) {
                    // merge pm to mm (but only if pm was already created)
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
                    S.add(ptr);
            }
        }

        S.add(INVALIDATED);
        S1.swap(S);
    }

    static inline bool isInvalidTarget(const PSNode * const target) {
        return  target == INVALIDATED ||
                target == UNKNOWN_MEMORY ||
                target == NULLPTR;
    }

    bool handleInvalidateLocals(PSNode *node) {
        bool changed = false;
        for (PSNode *pred : node->getPredecessors()) {
            changed |= handleInvalidateLocals(node, pred);
        }
        return changed;
    }

    bool handleInvalidateLocals(PSNode *node, PSNode *pred)
    {
        bool changed = false;
        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have a memory map");
        MemoryMapT *pmm = pred->getData<MemoryMapT>();
        assert(pmm && "Node's predecessor does not have a memory map");

        for (auto& I : *pmm) {
            if (isInvalidTarget(I.first))
                continue;

            // get or create a memory object for this target
            std::unique_ptr<MemoryObject>& moptr = (*mm)[I.first];
            if (!moptr)
                moptr.reset(new MemoryObject(I.first));

            MemoryObject *mo = moptr.get();
            MemoryObject *pmo = I.second.get();

            for (auto& it : *mo) {
                // remove pointers to locals from the points-to set
                if (containsLocal(node, it.second)) {
                    replaceLocalWithInv(node, it.second);
                    changed = true;
                }
            }

            for (auto& it : *pmo) {
                PointsToSetT& predS = it.second;
                PointsToSetT& S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that may point to freed memory
                for (const auto& ptr : predS) {
                    PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target);
                    if (alloc && isLocal(alloc, node))
                        changed |= S.add(INVALIDATED);
                    else
                        changed |= S.add(ptr);
                }

                // keep the map clean
                if (S.empty()) {
                    mo->pointsTo.erase(it.first);
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
                S.add(ptr);
        }

        S.add(INVALIDATED);
        S1.swap(S);
    }

    bool invalidateMemory(PSNode *node) {
        bool changed = false;
        for (PSNode *pred : node->getPredecessors()) {
            changed |= invalidateMemory(node, pred);
        }
        return changed;
    }

    bool invalidateMemory(PSNode *node, PSNode *pred)
    {
        bool changed = false;

        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");
        MemoryMapT *pmm = pred->getData<MemoryMapT>();
        assert(pmm && "Node does not have memory map");

        PSNode *operand = node->getOperand(0);

        for (auto& I : *pmm) {
            if (isInvalidTarget(I.first))
                continue;

            // get or create a memory object for this target
            std::unique_ptr<MemoryObject>& moptr = (*mm)[I.first];
            if (!moptr)
                moptr.reset(new MemoryObject(I.first));

            MemoryObject *mo = moptr.get();
            MemoryObject *pmo = I.second.get();

            //remove references to invalidated memory from mo
            for (auto& it : *mo) {
                for (const auto& ptr : operand->pointsTo) {
                    if (ptr.isNull() || ptr.isUnknown() || ptr.isInvalidated())
                        continue;

                    if (pointsToTarget(it.second, ptr.target)) {
                        replaceTargetWithInv(it.second, ptr.target);
                        changed = true;
                    }
                }
            }

            // merge pointers from pmo to mo, but skip
            // the pointers that may point to the freed memory
            for (auto& it : *pmo) {
                PointsToSetT& predS = it.second;
                PointsToSetT& S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that may point to freed memory
                for (const auto& ptr : predS) {
                    if (operand->pointsTo.count(ptr) == 0)
                        changed |= S.add(ptr);
                    else
                        changed |= S.add(INVALIDATED);
                }

                // keep the map clean
                if (S.empty()) {
                    mo->pointsTo.erase(it.first);
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
