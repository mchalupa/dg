#ifndef DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_
#define DG_ANALYSIS_POINTS_TO_WITH_INVALIDATE_H_

#include "PointerAnalysisFS.h"
#include <cassert>

namespace dg {
namespace pta {

class PointerAnalysisFSInv : public PointerAnalysisFS {
    static bool canInvalidateMM(PSNode *n) {
        return isa<PSNodeType::FREE>(n) ||
               isa<PSNodeType::INVALIDATE_OBJECT>(n) ||
               isa<PSNodeType::INVALIDATE_LOCALS>(n);
    }

    static bool needsMerge(PSNode *n) {
        return canInvalidateMM(n) || PointerAnalysisFS::needsMerge(n);
    }

    static MemoryObject *getOrCreateMO(MemoryMapT *mm, PSNode *target) {
        std::unique_ptr<MemoryObject> &moptr = (*mm)[target];
        if (!moptr)
            moptr.reset(new MemoryObject(target));

        assert(mm->find(target) != mm->end());
        return moptr.get();
    }

  public:
    using MemoryMapT = PointerAnalysisFS::MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointerAnalysisFSInv(PointerGraph *ps, PointerAnalysisOptions opts)
            : PointerAnalysisFS(ps, opts.setInvalidateNodes(true)) {}

    // default options
    PointerAnalysisFSInv(PointerGraph *ps) : PointerAnalysisFSInv(ps, {}) {}

    // NOTE: we must override this method as it is using our "needsMerge"
    bool beforeProcessed(PSNode *n) override {
        MemoryMapT *mm = n->getData<MemoryMapT>();
        if (mm)
            return false;

        // on these nodes the memory map can change
        if (needsMerge(n)) { // root node
            mm = createMM();

            // if this is the root of the entry procedure,
            // we must propagate the points-to information
            // from the globals initialization
            if (n == PG->getEntry()->getRoot()) {
                mergeGlobalsState(mm, PG->getGlobals());
            }
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

    bool afterProcessed(PSNode *n) override {
        if (n->getType() == PSNodeType::INVALIDATE_LOCALS)
            return handleInvalidateLocals(n);
        if (n->getType() == PSNodeType::INVALIDATE_OBJECT)
            return invalidateMemory(n);
        if (n->getType() == PSNodeType::FREE)
            return handleFree(n);

        assert(n->getType() != PSNodeType::FREE &&
               n->getType() != PSNodeType::INVALIDATE_OBJECT &&
               n->getType() != PSNodeType::INVALIDATE_LOCALS);

        bool ret = PointerAnalysisFS::afterProcessed(n);

        // check that all pointers in this memory map
        // are initialized in all predecessors and
        // set them to invalidated otherwise
        MemoryMapT *mm = n->getData<MemoryMapT>();
        assert(mm && "Do not have memory map");
        if (n->predecessorsNum() > 1) {
            for (PSNode *p : n->predecessors()) {
                if (MemoryMapT *pm = p->getData<MemoryMapT>()) {
                    ret |= handleUninitialized(mm, pm);
                }
            }
        }
        return ret;
    }

    static bool isLocal(PSNodeAlloc *alloc, PSNode *where) {
        return !alloc->isHeap() && !alloc->isGlobal() &&
               alloc->getParent() == where->getParent();
    }

    static bool containsRemovableLocals(PSNode *where, PointsToSetT &S) {
        for (const auto &ptr : S) {
            if (ptr.isNull() || ptr.isUnknown() || ptr.isInvalidated())
                continue;

            if (PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target)) {
                if (isLocal(alloc, where) && knownInstance(alloc))
                    return true;
            }
        }

        return false;
    }

    // not very efficient
    static void replaceLocalsWithInv(PSNode *where, PointsToSetT &S1) {
        PointsToSetT S;

        for (const auto &ptr : S1) {
            if (ptr.isNull() || ptr.isUnknown() || ptr.isInvalidated())
                continue;

            if (PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target)) {
                // if this is not local pointer or it is,
                // but we do not know which instance is being destroyed,
                // then keep the pointer
                if (!isLocal(alloc, where) || !knownInstance(alloc))
                    S.add(ptr);
            }
        }

        S.add(INVALIDATED, 0);
        S1.swap(S);
    }

    static inline bool isInvalidTarget(const PSNode *const target) {
        return target == INVALIDATED || target == UNKNOWN_MEMORY ||
               target == NULLPTR;
    }

    static bool handleInvalidateLocals(PSNode *node) {
        bool changed = false;
        for (PSNode *pred : node->predecessors()) {
            changed |= handleInvalidateLocals(node, pred);
        }
        return changed;
    }

    static bool handleInvalidateLocals(PSNode *node, PSNode *pred) {
        MemoryMapT *pmm = pred->getData<MemoryMapT>();
        if (!pmm) {
            // predecessor was not processed yet
            return false;
        }

        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have a memory map");

        bool changed = false;
        for (auto &I : *pmm) {
            if (isInvalidTarget(I.first))
                continue;

            // get or create a memory object for this target

            MemoryObject *mo = getOrCreateMO(mm, I.first);
            MemoryObject *pmo = I.second.get();

            for (auto &it : *mo) {
                // remove pointers to locals from the points-to set
                if (containsRemovableLocals(node, it.second)) {
                    replaceLocalsWithInv(node, it.second);
                    assert(!containsRemovableLocals(node, it.second));
                    changed = true;
                }
            }

            for (auto &it : *pmo) {
                PointsToSetT &predS = it.second;
                if (predS.empty())
                    continue;

                PointsToSetT &S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that _must_ point to destroyed memory
                for (const auto &ptr : predS) {
                    PSNodeAlloc *alloc = PSNodeAlloc::get(ptr.target);
                    if (alloc && isLocal(alloc, node) && knownInstance(alloc)) {
                        changed |= S.add(INVALIDATED, 0);
                    } else
                        changed |= S.add(ptr);
                }

                assert(!S.empty());
            }
        }

        return changed;
    }

    static void replaceTargetWithInv(PointsToSetT &S1, PSNode *target) {
        PointsToSetT S;
        for (const auto &ptr : S1) {
            if (ptr.target != target)
                S.add(ptr);
        }

        S.add(INVALIDATED, 0);
        S1.swap(S);
    }

    static bool invalidateMemory(PSNode *node) {
        bool changed = false;
        for (PSNode *pred : node->predecessors()) {
            changed |= invalidateMemory(node, pred);
        }
        return changed;
    }

    static bool handleFree(PSNode *node) {
        bool changed = false;
        for (PSNode *pred : node->predecessors()) {
            changed |= invalidateMemory(node, pred, true /* is free */);
        }
        return changed;
    }

    // return true if we know the instance of the object
    // (allocations in loop or recursive calls may have
    // multiple instances)
    static bool knownInstance(const PSNode *node) { return !isOnLoop(node); }

    static bool invStrongUpdate(const PSNode *operand) {
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

        const auto &ptr = *(operand->pointsTo.begin());
        return !ptr.offset.isUnknown() && !isInvalidTarget(ptr.target) &&
               knownInstance(ptr.target);
    }

    ///
    // Check whether we can overwrite the memory object that was used to load
    // the pointer into free().
    // Return the PSNode that represents this memory object
    static PSNode *moFromFreeToOverwrite(PSNode *operand) {
        // Bail out if the operand has no pointers yet,
        // otherwise we can add invalidated imprecisely
        // (the rest of invalidateMemory would not perform strong update)
        if (operand->pointsTo.empty())
            return nullptr;

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
            if (invStrongUpdate(loadOp)) {
                return (*(loadOp->pointsTo.begin())).target;
            }
        }

        return nullptr;
    }

    static bool overwriteMOFromFree(MemoryMapT *mm, PSNode *target) {
        // if we know exactly which memory object
        // is being used for freeing the memory,
        // we can set it to invalidated
        auto *mo = getOrCreateMO(mm, target);
        if (mo->pointsTo.size() == 1) {
            auto &S = mo->pointsTo[0];
            if (S.size() == 1 && (*S.begin()).target == INVALIDATED) {
                return false; // no update
            }
        }

        mo->pointsTo.clear();
        mo->pointsTo[0].add(INVALIDATED, 0);
        return true;
    }

    static bool invalidateMemory(PSNode *node, PSNode *pred,
                                 bool is_free = false) {
        MemoryMapT *pmm = pred->getData<MemoryMapT>();
        if (!pmm) {
            // predecessor was not processed yet
            return false;
        }

        MemoryMapT *mm = node->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        bool changed = false;

        PSNode *operand = node->getOperand(0);
        PSNode *strong_update = nullptr;
        // if we call e.g. free(load p), then the contents of
        // the memory pointed by p will point
        // to invalidated memory (we can do this when we
        // know precisely what is the memory).
        if (is_free) {
            strong_update = moFromFreeToOverwrite(operand);
            if (strong_update)
                changed |= overwriteMOFromFree(mm, strong_update);
        }

        for (auto &I : *pmm) {
            assert(I.first && "nullptr as target");

            if (isInvalidTarget(I.first))
                continue;

            // strong update on this variable?
            if (strong_update == I.first)
                continue;

            // get or create a memory object for this target
            MemoryObject *mo = getOrCreateMO(mm, I.first);
            MemoryObject *pmo = I.second.get();

            // Remove references to invalidated memory from mo
            // if the invalidated object is just one.
            // Otherwise, add the invalidated pointer to the points-to sets
            // (strong vs. weak update) as we do not know which
            // object is actually being invalidated.
            for (auto &it : *mo) {
                if (invStrongUpdate(operand)) { // strong update
                    const auto &ptr = *(operand->pointsTo.begin());
                    if (ptr.isUnknown())
                        changed |= it.second.add(INVALIDATED, 0);
                    else if (ptr.isNull() || ptr.isInvalidated())
                        continue;
                    else if (it.second.pointsToTarget(ptr.target)) {
                        replaceTargetWithInv(it.second, ptr.target);
                        assert(!it.second.pointsToTarget(ptr.target));
                        changed = true;
                    }
                } else { // weak update
                    for (const auto &ptr : operand->pointsTo) {
                        if (ptr.isNull() || ptr.isInvalidated())
                            continue;

                        // invalidate on unknown memory yields invalidate for
                        // each element
                        if (ptr.isUnknown() ||
                            it.second.pointsToTarget(ptr.target)) {
                            changed |= it.second.add(INVALIDATED, 0);
                        }
                    }
                }
            }

            // merge pointers from pmo to mo, but skip
            // the pointers that may point to the freed memory
            for (auto &it : *pmo) {
                PointsToSetT &predS = it.second;
                if (predS.empty()) // keep the map clean
                    continue;

                PointsToSetT &S = mo->pointsTo[it.first];

                // merge pointers from the previous states
                // but do not include the pointers
                // that may point to freed memory.
                // These must be replaced with invalidated.
                for (const auto &ptr : predS) {
                    if (ptr.isValid() && // if the ptr is null or unkown,
                                         // we want to copy it
                        operand->pointsTo.pointsToTarget(ptr.target)) {
                        if (!invStrongUpdate(operand)) {
                            // we still want to copy the original pointer
                            // if we cannot perform strong update
                            // on this invalidated memory
                            changed |= S.add(ptr);
                        }
                        changed |= S.add(INVALIDATED, 0);
                    } else {
                        // this is a pointer to some memory that was not
                        // invalidated, so merge it into the points-to set
                        changed |= S.add(ptr);
                    }
                }

                assert(!S.empty());
            }
        }

        return changed;
    }

    bool handleUninitialized(MemoryMapT *mm, MemoryMapT *pm) {
        bool changed = false;
        for (auto &it : *mm) {
            auto pmit = pm->find(it.first);
            if (pmit == pm->end()) {
                for (auto &mit : *it.second) {
                    if (mit.first.isUnknown())
                        continue; // FIXME: we are optimistic here...
                    changed |= it.second->addPointsTo(mit.first,
                                                      Pointer{INVALIDATED, 0});
                }
                continue;
            }

            /* check the initialization of memory objects
            auto *pmo = pmit->second.get();
            for (auto &mit : *it.second) {
                if (pmo->find(mit.first) != pmo->end())
                    continue;
                if (mit.first.isUnknown())
                    continue; // FIXME: we are optimistic here...

                changed |= it.second->addPointsTo(mit.first,
                                                  Pointer{INVALIDATED, 0});
            }
            */
        }

        return changed;
    }
};

} // namespace pta
} // namespace dg

#endif
