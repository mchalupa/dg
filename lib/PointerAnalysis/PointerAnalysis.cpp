#include "dg/PointerAnalysis/PointerAnalysis.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointsToSet.h"

#include "dg/util/debug.h"

namespace dg {
namespace pta {

// Return true if it makes sense to dereference this pointer.
// PTA is over-approximation, so this is a filter.
static inline bool canBeDereferenced(const Pointer &ptr) {
    if (!ptr.isValid() || ptr.isInvalidated() || ptr.isUnknown())
        return false;

    // if the pointer points to a function, we can not dereference it
    if (ptr.target->getType() == PSNodeType::FUNCTION)
        return false;

    return true;
}

static bool funHasAddressTaken(PSNode *node) {
    if (node->getType() != PSNodeType::FUNCTION)
        return false;

    return dg::any_of(node->getUsers(), [node](PSNode *user) {
        return node->getType() != PSNodeType::CALL ||
               // call but is not called
               node->getOperand(0) != user;
    });
}

bool PointerAnalysis::processLoad(PSNode *node) {
    bool changed = false;
    PSNode *operand = node->getOperand(0);

    if (operand->pointsTo.empty())
        return error(operand, "Load's operand has no points-to set");

    for (const Pointer &ptr : operand->pointsTo) {
        if (ptr.isUnknown()) {
            // load from unknown pointer yields unknown pointer
            changed |= node->addPointsTo(UnknownPointer);
            continue;
        }

        if (!canBeDereferenced(ptr))
            continue;

        // find memory objects holding relevant points-to
        // information
        std::vector<MemoryObject *> objects;
        getMemoryObjects(node, ptr, objects);

        PSNodeAlloc *target = PSNodeAlloc::get(ptr.target);
        assert(target && "Target is not memory allocation");

        // no objects found for this target? That is
        // load from unknown memory
        if (objects.empty()) {
            if (target->isZeroInitialized())
                // if the memory is zero initialized, then everything
                // is fine, we add nullptr
                changed |= node->addPointsTo(NullPointer);
            else
                changed |= errorEmptyPointsTo(node, target);

            continue;
        }

        for (MemoryObject *o : objects) {
            // is the offset to the memory unknown?
            // In that case everything can be referenced,
            // so we need to copy the whole points-to
            if (ptr.offset.isUnknown()) {
                // we should load from memory that has
                // no pointers in it - it may be an error
                // FIXME: don't duplicate the code
                if (o->pointsTo.empty()) {
                    if (target->isZeroInitialized())
                        changed |= node->addPointsTo(NullPointer);
                    else if (objects.size() == 1)
                        changed |= errorEmptyPointsTo(node, target);
                }

                // we have some pointers - copy them all,
                // since the offset is unknown
                for (auto &it : o->pointsTo) {
                    changed |= node->addPointsTo(it.second);
                }

                // this is all that we can do here...
                continue;
            }

            // load from empty points-to set
            // - that is load from unknown memory
            auto it = o->pointsTo.find(ptr.offset);
            if (it == o->pointsTo.end()) {
                // if the memory is zero initialized, then everything
                // is fine, we add nullptr
                if (target->isZeroInitialized())
                    changed |= node->addPointsTo(NullPointer);
                // if we don't have a definition even with unknown offset
                // it is an error
                // FIXME: don't triplicate the code!
                else if (!o->pointsTo.count(Offset::UNKNOWN))
                    changed |= errorEmptyPointsTo(node, target);
            } else {
                // we have pointers on that memory, so we can
                // do the work
                changed |= node->addPointsTo(it->second);
            }

            // plus always add the pointers at unknown offset,
            // since these can be what we need too
            it = o->pointsTo.find(Offset::UNKNOWN);
            if (it != o->pointsTo.end()) {
                changed |= node->addPointsTo(it->second);
            }
        }
    }

    return changed;
}

bool PointerAnalysis::processMemcpy(PSNode *node) {
    bool changed = false;
    PSNodeMemcpy *memcpy = PSNodeMemcpy::get(node);
    PSNode *srcNode = memcpy->getSource();
    PSNode *destNode = memcpy->getDestination();

    std::vector<MemoryObject *> srcObjects;
    std::vector<MemoryObject *> destObjects;

    // gather srcNode pointer objects
    for (const Pointer &ptr : srcNode->pointsTo) {
        assert(ptr.target && "Got nullptr as target");

        if (!canBeDereferenced(ptr))
            continue;

        srcObjects.clear();
        getMemoryObjects(node, ptr, srcObjects);

        if (srcObjects.empty()) {
            abort();
            return changed;
        }

        // gather destNode objects
        for (const Pointer &dptr : destNode->pointsTo) {
            assert(dptr.target && "Got nullptr as target");

            if (!canBeDereferenced(dptr))
                continue;

            destObjects.clear();
            getMemoryObjects(node, dptr, destObjects);

            if (destObjects.empty()) {
                abort();
                return changed;
            }

            changed |= processMemcpy(srcObjects, destObjects, ptr, dptr,
                                     memcpy->getLength());
        }
    }

    return changed;
}

static Pointer unwrapConstants(const Pointer &ptr) {
    Offset offset = ptr.offset;
    PSNode *target = ptr.target;
    while (auto *C = PSNodeConstant::get(target)) {
        target = C->getTarget();
        offset += C->getOffset();
    }
    return {target, offset};
}

bool PointerAnalysis::processMemcpy(std::vector<MemoryObject *> &srcObjects,
                                    std::vector<MemoryObject *> &destObjects,
                                    const Pointer &sptr, const Pointer &dptr,
                                    Offset len) {
    assert(len > 0 && "Memcpy of length 0");

    bool changed = false;

    Pointer tmp = unwrapConstants(sptr);
    Offset srcOffset = tmp.offset;
    PSNodeAlloc *sourceAlloc = PSNodeAlloc::get(tmp.target);
    assert(sourceAlloc && "Source in memcpy is invalid");

    tmp = unwrapConstants(dptr);
    Offset destOffset = tmp.offset;
    PSNodeAlloc *destAlloc = PSNodeAlloc::get(tmp.target);
    assert(destAlloc && "Destination in memcpy is invalid");

    // set to true if the contents of destination memory
    // can contain null
    bool contains_null_somewhere = false;

    // if the source is zero initialized, we may copy null pointer
    if (sourceAlloc->isZeroInitialized()) {
        // if we really copy the whole object, just set it zero-initialized
        if ((sourceAlloc->getSize() != Offset::UNKNOWN) &&
            (sourceAlloc->getSize() == destAlloc->getSize()) &&
            len == sourceAlloc->getSize() && sptr.offset == 0) {
            destAlloc->setZeroInitialized();
        } else {
            // we could analyze in a lot of cases where
            // shoulde be stored the nullptr, but the question
            // is whether it is worth it... For now, just say
            // that somewhere may be null in the destination
            contains_null_somewhere = true;
        }
    }

    for (MemoryObject *destO : destObjects) {
        if (contains_null_somewhere)
            changed |= destO->addPointsTo(Offset::UNKNOWN, NullPointer);

        // copy every pointer from srcObjects that is in
        // the range to destination's objects
        for (MemoryObject *so : srcObjects) {
            for (auto &src : so->pointsTo) { // src.first is offset,
                                             // src.second is a PointToSet

                // if the offset is inbound of the copied memory
                // or we copy from unknown offset, or this pointer
                // is on unknown offset, copy this pointer
                if (src.first.isUnknown() || srcOffset.isUnknown() ||
                    (srcOffset <= src.first &&
                     (len.isUnknown() || *src.first - *srcOffset < *len))) {
                    // copy the pointer, but shift it by the offsets
                    // we are working with
                    if (!src.first.isUnknown() && !srcOffset.isUnknown() &&
                        !destOffset.isUnknown()) {
                        // check that new offset does not overflow
                        // Offset::UNKNOWN
                        if (Offset::UNKNOWN - *destOffset <=
                            *src.first - *srcOffset) {
                            changed |= destO->addPointsTo(Offset::UNKNOWN,
                                                          src.second);
                            continue;
                        }

                        Offset newOff = *src.first - *srcOffset + *destOffset;
                        if (newOff >= destO->node->getSize() ||
                            newOff >= options.fieldSensitivity) {
                            changed |= destO->addPointsTo(Offset::UNKNOWN,
                                                          src.second);
                        } else {
                            changed |= destO->addPointsTo(newOff, src.second);
                        }
                    } else {
                        changed |=
                                destO->addPointsTo(Offset::UNKNOWN, src.second);
                    }
                }
            }
        }
    }

    return changed;
}

bool PointerAnalysis::processGep(PSNode *node) {
    bool changed = false;

    PSNodeGep *gep = PSNodeGep::get(node);
    assert(gep && "Non-GEP given");

    for (const Pointer &ptr : gep->getSource()->pointsTo) {
        Offset::type new_offset;
        if (ptr.offset.isUnknown() || gep->getOffset().isUnknown())
            // set it like this to avoid overflow when adding
            new_offset = Offset::UNKNOWN;
        else
            new_offset = *ptr.offset + *gep->getOffset();

        // in the case PSNodeType::the memory has size 0, then every pointer
        // will have unknown offset with the exception that it points
        // to the begining of the memory - therefore make 0 exception
        if ((new_offset == 0 || new_offset < ptr.target->getSize()) &&
            new_offset < *options.fieldSensitivity)
            changed |= node->addPointsTo(ptr.target, new_offset);
        else
            changed |= node->addPointsTo(ptr.target, Offset::UNKNOWN);
    }

    return changed;
}

bool PointerAnalysis::processNode(PSNode *node) {
    bool changed = false;
    std::vector<MemoryObject *> objects;

#ifdef DEBUG_ENABLED
    size_t prev_size = node->pointsTo.size();
#endif

    switch (node->type) {
    case PSNodeType::LOAD:
        changed |= processLoad(node);
        break;
    case PSNodeType::STORE:
        for (const Pointer &ptr : node->getOperand(1)->pointsTo) {
            assert(ptr.target && "Got nullptr as target");

            if (!canBeDereferenced(ptr))
                continue;

            objects.clear();
            getMemoryObjects(node, ptr, objects);
            for (MemoryObject *o : objects) {
                changed |= o->addPointsTo(ptr.offset,
                                          node->getOperand(0)->pointsTo);
            }
        }
        break;
    case PSNodeType::INVALIDATE_OBJECT:
    case PSNodeType::FREE:
        break;
    case PSNodeType::INVALIDATE_LOCALS:
        // FIXME: get rid of this type of node
        // (make the analysis extendable and move it there)
        node->setParent(node->getOperand(0)->getSingleSuccessor()->getParent());
        break;
    case PSNodeType::GEP:
        changed |= processGep(node);
        break;
    case PSNodeType::CAST:
        // cast only copies the pointers
        changed |= node->addPointsTo(node->getOperand(0)->pointsTo);
        break;
    case PSNodeType::CONSTANT:
        // maybe warn? It has no sense to insert the constants into the graph.
        // On the other hand it is harmless. We can at least check if it is
        // correctly initialized 8-)
        assert(node->pointsTo.size() == 1 &&
               "Constant should have exactly one pointer");
        break;
    case PSNodeType::CALL_RETURN:
        if (options.invalidateNodes) {
            for (PSNode *op : node->operands) {
                for (const Pointer &ptr : op->pointsTo) {
                    if (!canBeDereferenced(ptr))
                        continue;
                    PSNodeAlloc *target = PSNodeAlloc::get(ptr.target);
                    assert(target && "Target is not memory allocation");
                    if (!target->isHeap() && !target->isGlobal()) {
                        changed |= node->addPointsTo(INVALIDATED, 0);
                    }
                }
            }
        }
        // fall-through
    case PSNodeType::RETURN:
        // gather pointers returned from subprocedure - the same way
        // as PHI works
    case PSNodeType::PHI:
        for (PSNode *op : node->operands)
            changed |= node->addPointsTo(op->pointsTo);
        break;
    case PSNodeType::CALL_FUNCPTR:
        // call via function pointer:
        // first gather the pointers that can be used to the
        // call and if something changes, let backend take some action
        // (for example build relevant subgraph)
        for (const Pointer &ptr : node->getOperand(0)->pointsTo) {
            // do not add pointers that do not point to functions
            // (but do not do that when we are looking for invalidated
            // memory as this may lead to undefined behavior)
            if (!options.invalidateNodes &&
                ptr.target->getType() != PSNodeType::FUNCTION)
                continue;
            // Functions that have not address taken cannot
            // be called via a pointer
            if (!funHasAddressTaken(ptr.target)) {
                continue;
            }
            if (node->addPointsTo(ptr)) {
                changed = true;

                if (ptr.isValid() && !ptr.isInvalidated()) {
                    functionPointerCall(node, ptr.target);
                } else {
                    error(node, "Calling invalid pointer as a function!");
                    continue;
                }
            }
        }
        break;
    case PSNodeType::FORK: // FORK works basically the same as FUNCPTR
        for (const Pointer &ptr : node->getOperand(0)->pointsTo) {
            // do not add pointers that do not point to functions
            // (but do not do that when we are looking for invalidated
            // memory as this may lead to undefined behavior)
            if (!options.invalidateNodes &&
                ptr.target->getType() != PSNodeType::FUNCTION)
                continue;

            if (node->addPointsTo(ptr)) {
                changed = true;

                if (ptr.isValid() && !ptr.isInvalidated()) {
                    handleFork(node, ptr.target);
                } else {
                    error(node, "Calling invalid pointer in fork!");
                    continue;
                }
            }
        }
        break;
    case PSNodeType::JOIN:
        changed |= handleJoin(node);
        break;
    case PSNodeType::MEMCPY:
        changed |= processMemcpy(node);
        break;
    case PSNodeType::ALLOC:
    case PSNodeType::FUNCTION:
        // these two always points to itself
        assert(node->doesPointsTo(node, 0));
        assert(node->pointsTo.size() == 1);
    case PSNodeType::CALL:
    case PSNodeType::ENTRY:
    case PSNodeType::NOOP:
        // just no op
        break;
    default:
        assert(0 && "Unknown type");
    }

#ifdef DEBUG_ENABLED
    // the change of points-to set is not the only
    // change that can happen, so we don't use it as an
    // indicator and we use the 'changed' variable instead.
    // However, this assertion must hold:
    assert((node->pointsTo.size() == prev_size || changed) &&
           "BUG: Did not set change but changed points-to sets");
#endif

    return changed;
}

void PointerAnalysis::sanityCheck() {
#ifndef NDEBUG
    assert(NULLPTR->pointsTo.size() == 1 && "Null has been assigned a pointer");
    assert(NULLPTR->doesPointsTo(NULLPTR) &&
           "Null points to a different location");
    assert(UNKNOWN_MEMORY->pointsTo.size() == 1 &&
           "Unknown memory has been assigned a pointer");
    assert(UNKNOWN_MEMORY->doesPointsTo(UNKNOWN_MEMORY, Offset::UNKNOWN) &&
           "Unknown memory has been assigned a pointer");
    assert(INVALIDATED->pointsTo.empty() &&
           "Unknown memory has been assigned a pointer");

    auto nodes = PG->getNodes(PG->getEntry()->getRoot());
    std::set<unsigned> ids;
    for (auto *nd : nodes) {
        assert(ids.insert(nd->getID()).second && "Duplicated node ID");

        if (nd->getType() == PSNodeType::ALLOC) {
            assert(nd->pointsTo.size() == 1 &&
                   "Alloc does not point only to itself");
            assert(nd->doesPointsTo(nd, 0) &&
                   "Alloc does not point only to itself");
        }
    }
#endif // not NDEBUG
}

static void setToEmpty(std::vector<PSNode *> &nodes) {
    for (auto *n : nodes) {
        if (n->getType() != PSNodeType::ALLOC &&
            n->getType() != PSNodeType::CONSTANT) {
            n->pointsTo.clear();
        }
    }
}

bool PointerAnalysis::run() {
    DBG_SECTION_BEGIN(pta, "Running pointer analysis");

    preprocess();

    // check that the current state of pointer analysis makes sense
    sanityCheck();

    // process global nodes, these must reach fixpoint after one iteration
    DBG(pta, "Processing global nodes");
    queue_globals();
    iteration();
    assert((to_process.clear(), changed.clear(), queue_globals(),
            !iteration()) &&
           "Globals did not reach fixpoint");
    to_process.clear();
    changed.clear();

    initialize_queue();

    // override the pre-set value
    if (options.maxIterations > 0) {
        DBG(pta, "The maximal number of iterations is set to "
                         << options.maxIterations);
    }

    size_t n = 0;
    // do fixpoint
    do {
        if (options.maxIterations > 0 && n > options.maxIterations) {
            DBG(pta, "Reached the maximum number of iterations: " << n);
            setToEmpty(to_process);
            to_process.clear();
            break;
        }
#if DEBUG_ENABLED
#define DUMP_NTH_ITER 100
        if (n % DUMP_NTH_ITER == 0) {
            DBG(pta, "Iteration " << n << ", queue size " << to_process.size());
        }
#endif
        ++n;

        iteration();
        queue_changed();
    } while (!to_process.empty());

    DBG(pta, "Reached fixpoint after " << n << " iterations\n");

    assert(to_process.empty());
    assert(changed.empty());

    // NOTE: With flow-insensitive analysis, it may happen that
    // we have not reached the fixpoint here. This is beacuse
    // we queue only reachable nodes from the nodes that changed
    // something. So if in the rechable nodes something generates
    // new information, than this information could be added to some
    // node in a new iteration over all nodes. But this information
    // can never get to that node in runtime, since that node is
    // unreachable from the point where the information is
    // generated, so this is OK.

    sanityCheck();

    DBG_SECTION_END(pta, "Running pointer analysis done");

    return options.maxIterations > 0 ? n <= options.maxIterations : true;
}

} // namespace pta
} // namespace dg
