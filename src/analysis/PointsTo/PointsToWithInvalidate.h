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

    static MemoryObject *getOrCreateMO(MemoryMapT *mm, PSNode *target) {
        std::unique_ptr<MemoryObject>& moptr = (*mm)[target];
        if (!moptr)
            moptr.reset(new MemoryObject(target));

        assert(mm->find(target) != mm->end());
        return moptr.get();
    }

public:
    using MemoryMapT = PointsToFlowSensitive::MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointsToWithInvalidate(PointerSubgraph *ps,
                           PointerAnalysisOptions opts)
    : PointsToFlowSensitive(ps, opts.setInvalidateNodes(true)) {}

    // default options
    PointsToWithInvalidate(PointerSubgraph *ps) : PointsToWithInvalidate(ps, {}) {}

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

            MemoryObject *mo = getOrCreateMO(mm, I.first);
            MemoryObject *pmo = I.second.get();

            for (auto& it : *mo) {
                // remove pointers to locals from the points-to set
                if (containsLocal(node, it.second)) {
                    replaceLocalWithInv(node, it.second);
                    assert(!containsLocal(node, it.second));
                    changed = true;
                }
            }

            for (auto& it : *pmo) {
                PointsToSetT& predS = it.second;
                if (predS.empty())
                    continue;

                PointsToSetT& S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that may point to freed memory
                for (const auto& ptr : predS) {
                    PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target);
                    if (alloc && isLocal(alloc, node)) {
                        changed |= S.add(INVALIDATED);
                    } else
                        changed |= S.add(ptr);
                }

                assert(!S.empty());
            }
        }

        return changed;
    }

    static inline bool pointsToTarget(PointsToSetT& S, PSNode *target) {
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

    bool invStrongUpdate(PSNode * /*operand*/) {
        // This is not right as we do not know to which instance
        // of the object the pointer points to
        // (the allocation may be in a loop)
        //
        // return operand->pointsTo.size() == 1;

        // TODO: but we can do strong update on must-aliases
        // of the invalidated pointer. That is, e.g. for
        // free(p), we may do strong update for q if q is must-alias
        // of p (no matter the size of p's and q's points-to sets)
        return false;
    }

    bool overwriteInvalidatedVariable(MemoryMapT *mm, PSNode *operand) {
        // Bail out if the operand has no pointers yet,
        // otherwise we can add invalidated imprecisely
        // (the rest of invalidateMemory would not perform strong update)
        if (operand->pointsTo.empty())
            return false;

        // invalidate(p) translates to
        //  1 = load x
        //  ...
        //  invalidate(1)
        // Get objects where x may point to. If this object is only one,
        // then we know that this object will point to invalid memory
        // (no what is its state).
        PSNode *strippedOp = operand->stripCasts();
        if (strippedOp->getType() == PSNodeType::LOAD) {
            // get the pointer to the memory that holds the pointers
            // that are being freed
            PSNode *loadOp = strippedOp->getOperand(0);
            if (loadOp->pointsTo.size() == 1) {
                // if we know exactly which memory object
                // is being used for freeing the memory,
                // we can set it to invalidated
                const auto& ptr = *(loadOp->pointsTo.begin());
                auto mo = getOrCreateMO(mm, ptr.target);
                if (mo->pointsTo.size() == 1) {
                    auto& S = mo->pointsTo[0];
                    if (S.size() == 1 && (*S.begin()).target == INVALIDATED) {
                        return false;
                    }
                }

                mo->pointsTo.clear();
                mo->pointsTo[0].add(INVALIDATED);
                return true;
            }
        }

        return false;
    }

    bool invalidateMemory(PSNode *node, PSNode *pred)
    {
        bool changed = false;

        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");
        MemoryMapT *pmm = pred->getData<MemoryMapT>();
        assert(pmm && "Node does not have memory map");

        PSNode *operand = node->getOperand(0);
        // if we call e.g. free(p), then p will point
        // to invalidated memory no matter how many places
        // it may have pointed before.
        changed |= overwriteInvalidatedVariable(mm, operand);

        for (auto& I : *pmm) {
            if (isInvalidTarget(I.first))
                continue;

            // get or create a memory object for this target
            MemoryObject *mo = getOrCreateMO(mm, I.first);
            MemoryObject *pmo = I.second.get();

            // Remove references to invalidated memory from mo
            // if the invalidated object is just one.
            // Otherwise, add the invalidated pointer to the points-to sets
            // (strong vs. weak update) as we do not know which
            // object is actually being invalidated.
            for (auto& it : *mo) {
                if (invStrongUpdate(operand)) { // strong update
                    const auto& ptr = *(operand->pointsTo.begin());
                    if (ptr.isUnknown())
                        changed |= it.second.add(INVALIDATED);
                    else if (ptr.isNull() || ptr.isInvalidated())
                        continue;
                    else if (pointsToTarget(it.second, ptr.target)) {
                        replaceTargetWithInv(it.second, ptr.target);
                        assert(!pointsToTarget(it.second, ptr.target));
                        changed = true;
                    }
                } else { // weak update
                    for (const auto& ptr : operand->pointsTo) {
                        if (ptr.isNull() || ptr.isInvalidated())
                            continue;

                        // invalidate on unknown memory yields invalidate for
                        // each element
                        if (ptr.isUnknown() || pointsToTarget(it.second, ptr.target)) {
                            changed |= it.second.add(INVALIDATED);
                        }
                    }
                }
            }

            // merge pointers from pmo to mo, but skip
            // the pointers that may point to the freed memory
            for (auto& it : *pmo) {
                PointsToSetT& predS = it.second;
                if (predS.empty()) // keep the map clean
                    continue;

                PointsToSetT& S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that may point to freed memory
                for (const auto& ptr : predS) {
                    if (pointsToTarget(operand->pointsTo, ptr.target)) {
                        changed |= S.add(INVALIDATED);
                    } else {
                        // this pointer is to some memory that was not invalidated,
                        // so merge it into the points-to set
                        changed |= S.add(ptr);
                    }
                }

                assert(!S.empty());
            }
        }

        return changed;
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_
