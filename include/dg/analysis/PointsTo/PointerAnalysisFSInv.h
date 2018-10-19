#ifndef _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_
#define _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_

#include <cassert>
#include "PointerAnalysisFS.h"

namespace dg {
namespace analysis {
namespace pta {

class PointerAnalysisFSInv : public PointerAnalysisFS
{
    static bool canChangeMM(PSNode *n) {
        if (n->getType() == PSNodeType::FREE ||
            n->getType() == PSNodeType::INVALIDATE_OBJECT ||
            n->getType() == PSNodeType::INVALIDATE_LOCALS)
            return true;

        return PointerAnalysisFS::canChangeMM(n);
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

    PSNode *strongUpdateVariable{nullptr};

public:
    using MemoryMapT = PointerAnalysisFS::MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointerAnalysisFSInv(PointerSubgraph *ps,
                           PointerAnalysisOptions opts)
    : PointerAnalysisFS(ps, opts.setInvalidateNodes(true)) {}

    // default options
    PointerAnalysisFSInv(PointerSubgraph *ps) : PointerAnalysisFSInv(ps, {}) {}

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
        if (n->getType() == PSNodeType::INVALIDATE_LOCALS)
            return handleInvalidateLocals(n);
        if (n->getType() == PSNodeType::INVALIDATE_OBJECT)
            return invalidateMemory(n);
        if (n->getType() == PSNodeType::FREE)
            return invalidateMemory(n);

        assert(n->getType() != PSNodeType::FREE &&
               n->getType() != PSNodeType::INVALIDATE_OBJECT &&
               n->getType() != PSNodeType::INVALIDATE_LOCALS);

        return PointerAnalysisFS::afterProcessed(n);
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

    bool invStrongUpdate(PSNode *operand) {
        // If we are freeing memory through node that
        // points to precisely known valid memory that is not allocated
        // on a loop, we can do strong update.
        //
        // TODO: we can do strong update also on must-aliases
        // of the invalidated pointer. That is, e.g. for
        // free(p), we may do strong update for q if q is must-alias
        // of p (no matter the size of p's and q's points-to sets)
        if (operand->pointsTo.size() != 1)
            return false;

        const auto& ptr  = *(operand->pointsTo.begin());
        return !ptr.offset.isUnknown()
                && !isInvalidTarget(ptr.target) && !isOnLoop(ptr.target);
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
                strongUpdateVariable = (*(loadOp->pointsTo.begin())).target;

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

            // strong update on this variable?
            if (strongUpdateVariable && strongUpdateVariable == I.first)
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
                // that may point to freed memory.
                // These must be replaced with invalidated.
                for (const auto& ptr : predS) {
                    if (ptr.isValid() && // if the ptr is null or unkown,
                                         // we want to copy it
                        pointsToTarget(operand->pointsTo, ptr.target)) {
                        if (!invStrongUpdate(operand)) {
                            // we still want to copy the original pointer
                            // if we cannot perform strong update
                            // on this invalidated memory
                            changed |= S.add(ptr);
                        }
                        changed |= S.add(INVALIDATED);
                    } else {
                        // this is a pointer to some memory that was not
                        // invalidated, so merge it into the points-to set
                        changed |= S.add(ptr);
                    }
                }

                assert(!S.empty());
            }
        }

        // reset strong update!
        strongUpdateVariable = nullptr;
        return changed;
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_
