#include "Pointer.h"
#include "PointerSubgraph.h"
#include "PointerAnalysis.h"

namespace dg {
namespace analysis {
namespace pta {

// nodes representing NULL, unknown memory
// and invalidated memory
PSNode NULLPTR_LOC(PSNodeType::NULL_ADDR);
PSNode *NULLPTR = &NULLPTR_LOC;
PSNode UNKNOWN_MEMLOC(PSNodeType::UNKNOWN_MEM);
PSNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;
PSNode INVALIDATED_LOC(PSNodeType::INVALIDATED);
PSNode *INVALIDATED = &INVALIDATED_LOC;

// pointers to those memory
const Pointer PointerUnknown(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
const Pointer PointerNull(NULLPTR, 0);

// replace all pointers to given target with one
// to that target, but UNKNOWN_OFFSET
bool PSNode::addPointsToUnknownOffset(PSNode *target)
{
    bool changed = false;
    for (auto I = pointsTo.begin(), E = pointsTo.end(); I != E;) {
        auto cur = I++;

        // erase pointers to the same memory but with concrete offset
        if (cur->target == target && !cur->offset.isUnknown()) {
            pointsTo.erase(cur);
            changed = true;
        }
    }

    // DONT use addPointsTo() method, it would recursively call
    // this method again, until stack overflow
    changed |= pointsTo.insert(Pointer(target, UNKNOWN_OFFSET)).second;

    return changed;
}

bool PointerAnalysis::processLoad(PSNode *node)
{
    bool changed = false;
    PSNode *operand = node->getOperand(0);

    if (operand->pointsTo.empty())
        return error(operand, "Load's operand has no points-to set");

    for (const Pointer& ptr : operand->pointsTo) {
        if (ptr.isNull())
            continue;

        if (ptr.isUnknown()) {
            // load from unknown pointer yields unknown pointer
            changed |= node->addPointsTo(UNKNOWN_MEMORY);
            continue;
        }

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
                changed |= node->addPointsTo(NULLPTR);
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
                        changed |= node->addPointsTo(NULLPTR);
                    else if (objects.size() == 1)
                        changed |= errorEmptyPointsTo(node, target);
                }

                // we have some pointers - copy them all,
                // since the offset is unknown
                for (auto& it : o->pointsTo) {
                    for (const Pointer &p : it.second) {
                        changed |= node->addPointsTo(p);
                    }
                }

                // this is all that we can do here...
                continue;
            }

            // load from empty points-to set
            // - that is load from unknown memory
            if (!o->pointsTo.count(ptr.offset)) {
                // if the memory is zero initialized, then everything
                // is fine, we add nullptr
                if (target->isZeroInitialized())
                    changed |= node->addPointsTo(NULLPTR);
                // if we don't have a definition even with unknown offset
                // it is an error
                // FIXME: don't triplicate the code!
                else if (!o->pointsTo.count(UNKNOWN_OFFSET))
                    changed |= errorEmptyPointsTo(node, target);
            } else {
                // we have pointers on that memory, so we can
                // do the work
                for (const Pointer& memptr : o->pointsTo[ptr.offset])
                    changed |= node->addPointsTo(memptr);
            }

            // plus always add the pointers at unknown offset,
            // since these can be what we need too
            if (o->pointsTo.count(UNKNOWN_OFFSET)) {
                for (const Pointer& memptr : o->pointsTo[UNKNOWN_OFFSET]) {
                    changed |= node->addPointsTo(memptr);
                }
            }
        }
    }

    return changed;
}

bool PointerAnalysis::processMemcpy(PSNode *node)
{
    bool changed = false;
    PSNodeMemcpy *memcpy = PSNodeMemcpy::get(node);

    // what to copy
    std::vector<MemoryObject *> srcObjects;
    // where to copy
    std::vector<MemoryObject *> destObjects;
    PSNode *srcNode = memcpy->getSource();
    PSNode *destNode = memcpy->getDestination();

    /* if one is zero initialized and we copy it whole,
     * set the other zero initialized too.
     * FIXME: this should be  check on the pointed memory,
     * not on the operands 
        if ((!destNode->isZeroInitialized() && srcNode->isZeroInitialized())
            && (memcpy->getSourceOffset() == 0 &&
                (memcpy->getLength().isUnknown() ||
                 memcpy->getLength() == srcNode->getSize()))) {
            destNode->setZeroInitialized();
            changed = true;
        }
        */

    // gather srcNode pointer objects
    for (const Pointer& ptr : srcNode->pointsTo) {
        assert(ptr.target && "Got nullptr as target");

        if (!ptr.isValid())
            continue;

        getMemoryObjects(node, ptr, srcObjects);
    }

    // gather destNode objects
    for (const Pointer& dptr : destNode->pointsTo) {
        assert(dptr.target && "Got nullptr as target");

        if (!dptr.isValid())
            continue;

        getMemoryObjects(node, dptr, destObjects);
    }

    if (srcObjects.empty()){
        // FIXME: we need to solve this -- this should
        // not be checked on srcNode, but on the pointers
        // (and write a test for me
        abort();

/*
        if (srcNode->isZeroInitialized()) {
            // if the memory is zero initialized,
            // then everything is fine, we add nullptr
            changed |= node->addPointsTo(NULLPTR);
        } else {
            changed |= errorEmptyPointsTo(node, srcNode);
        }
        */

        return changed;
    }

    for (MemoryObject *o : destObjects) {
        // copy every pointer from srcObjects that is in
        // the range to these objects
        for (MemoryObject *so : srcObjects) {
            for (auto& src : so->pointsTo) {
                // src.first is offset, src.second is a PointToSet

                // we need to copy ptrs at UNKNOWN_OFFSET always
                if (src.first.isUnknown() || memcpy->getSourceOffset().isUnknown()) {
                    changed |= o->addPointsTo(src.first, src.second);
                    continue;
                }

                if (memcpy->getLength().isUnknown()) {
                    if (*src.first < *memcpy->getSourceOffset())
                        continue;
                } else {
                    if (!src.first.inRange(*memcpy->getSourceOffset(),
                                           *memcpy->getSourceOffset() + *memcpy->getLength() - 1))
                    continue;
                }

                changed |= o->addPointsTo(src.first, src.second);
            }
        }

        // we need to take care of the case when src is zero initialized,
        // but points-to somewhere, imagine this:
        //
        // struct s { ptr1, ptr2, ptr3 };
        // struct s1 = {0}; /* s1 is zero initialized */
        // struct s1.ptr1 = &a;
        // struct s2;
        // memcpy(s1, s2, 0, 16); /* copy first two pointers */
        //
        // in this case s2 will point to 'a' at offset 0, but won't
        // point to null at offset 8, but it should... fix it by adding
        // nullptr at UNKNOWN_OFFSET (we may loose precision, but we'll
        // be sound)
        //
        /* FIXME and write test for me:
        if (srcNode->isZeroInitialized()
            && !((*memcpy->getSourceOffset() == 0 && memcpy->getLength().isUnknown())
                 || memcpy->getSourceOffset().isUnknown()))
            // src is zeroed and we don't copy whole memory?
            changed |= o->addPointsTo(UNKNOWN_OFFSET, NULLPTR);
            */
    }

    return changed;
}

bool PointerAnalysis::processGep(PSNode *node) {
    bool changed = false;

    PSNodeGep *gep = PSNodeGep::get(node);
    assert(gep && "Non-GEP given");

    for (const Pointer& ptr : gep->getSource()->pointsTo) {
        uint64_t new_offset;
        if (ptr.offset.isUnknown() || gep->getOffset().isUnknown())
            // set it like this to avoid overflow when adding
            new_offset = UNKNOWN_OFFSET;
        else
            new_offset = *ptr.offset + *gep->getOffset();
    
        // in the case PSNodeType::the memory has size 0, then every pointer
        // will have unknown offset with the exception that it points
        // to the begining of the memory - therefore make 0 exception
        if ((new_offset == 0 || new_offset < ptr.target->getSize())
            && new_offset < max_offset)
            changed |= node->addPointsTo(ptr.target, new_offset);
        else
            changed |= node->addPointsToUnknownOffset(ptr.target);
    }

    return changed;
}

bool PointerAnalysis::processNode(PSNode *node)
{
    bool changed = false;
    std::vector<MemoryObject *> objects;

#ifdef DEBUG_ENABLED
    size_t prev_size = node->pointsTo.size();
#endif

    switch(node->type) {
        case PSNodeType::LOAD:
            changed |= processLoad(node);
            break;
        case PSNodeType::STORE:
            for (const Pointer& ptr : node->getOperand(1)->pointsTo) {
                PSNode *target = ptr.target;
                assert(target && "Got nullptr as target");

                if (ptr.isNull())
                    continue;

                objects.clear();
                getMemoryObjects(node, ptr, objects);
                for (MemoryObject *o : objects) {
                    for (const Pointer& to : node->getOperand(0)->pointsTo) {
                        changed |= o->addPointsTo(ptr.offset, to);
                    }
                }
            }
            break;
        case PSNodeType::FREE:
            for (const Pointer& ptr : node->getOperand(0)->pointsTo) {
                PSNode *target = ptr.target;
                assert(target && "Got nullptr as target");

                if (ptr.isNull())
                    continue;

                objects.clear();
                getMemoryObjectsPointingTo(node, ptr, objects);
                for (MemoryObject *o : objects) {
                    changed |= o->addPointsTo(UNKNOWN_OFFSET, INVALIDATED);
                }
            }
            break;
        case PSNodeType::INVALIDATE_LOCALS:
            node->setParent(node->getOperand(0)->getSingleSuccessor()->getParent());
            objects.clear();
            getLocalMemoryObjects(node, objects);
            for (MemoryObject *o : objects) {
                changed |= o->addPointsTo(UNKNOWN_OFFSET, INVALIDATED);
            }
            break;
        case PSNodeType::GEP:
            changed |= processGep(node); 
            break;
        case PSNodeType::CAST:
            // cast only copies the pointers
            for (const Pointer& ptr : node->getOperand(0)->pointsTo)
                changed |= node->addPointsTo(ptr);
            break;
        case PSNodeType::CONSTANT:
            // maybe warn? It has no sense to insert the constants into the graph.
            // On the other hand it is harmless. We can at least check if it is
            // correctly initialized 8-)
            assert(node->pointsTo.size() == 1
                   && "Constant should have exactly one pointer");
            break;
        case PSNodeType::CALL_RETURN:
            if (invalidate_nodes) {
                for (PSNode *op : node->operands) {
                    for (const Pointer& ptr : op->pointsTo) {
                        PSNodeAlloc *target = PSNodeAlloc::get(ptr.target);
                        assert(target && "Target is not memory allocation");
                        if (!target->isHeap() && !target->isGlobal())
                            changed |= node->addPointsTo(INVALIDATED);
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
            for (const Pointer& ptr : node->getOperand(0)->pointsTo) {
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
        case PSNodeType::MEMCPY:
            changed |= processMemcpy(node);
            break;
        case PSNodeType::ALLOC:
        case PSNodeType::DYN_ALLOC:
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
    assert((node->pointsTo.size() == prev_size || changed)
           && "BUG: Did not set change but changed points-to sets");
#endif

    return changed;
}

} // namespace pta
} // namespace analysis
} // namespace dg
