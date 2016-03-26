#include "Pointer.h"
#include "PSS.h"

namespace dg {
namespace analysis {
namespace pss {

// nodes representing NULL and unknown memory
PSSNode NULLPTR_LOC(NULL_ADDR);
PSSNode *NULLPTR = &NULLPTR_LOC;
PSSNode UNKNOWN_MEMLOC(UNKNOWN_MEM);
PSSNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

// pointers to those memory
const Pointer PointerUnknown(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
const Pointer PointerNull(NULLPTR, 0);

// replace all pointers to given target with one
// to that target, but UNKNOWN_OFFSET
bool PSSNode::addPointsToUnknownOffset(PSSNode *target)
{
    bool changed = false;
    // FIXME: use equal range, it is much faster
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

bool PSS::processLoad(PSSNode *node)
{
    bool changed = false;
    PSSNode *operand = node->getOperand(0);

    if (operand->pointsTo.empty())
        return errorEmptyPointsTo(node, operand);

    for (const Pointer& ptr : operand->pointsTo) {
        if (ptr.isNull())
            continue;

        PSSNode *target = ptr.target;
        assert(target && "Got nullptr as target");

        // find memory objects holding relevant points-to
        // information
        std::vector<MemoryObject *> objects;
        getMemoryObjects(node, target, objects);

        // no objects found for this target? That is
        // load from unknown memory
        if (objects.empty()) {
            if (target->isZeroInitialized())
                // if the memory is zero initialized, then everything
                // is fine, we add nullptr
                changed |= node->addPointsTo(NULLPTR);
            else
                errorEmptyPointsTo(node, target);

            continue;
        }

        for (MemoryObject *o : objects) {
            // is the offset to the memory unknown?
            // In that case everything can be referenced,
            // so we need to copy the whole points-to
            if (ptr.offset.isUnknown()) {
                // we should load from memory that has
                // no pointers in it - it may be an error
                if (o->pointsTo.empty()) {
                    if (target->isZeroInitialized())
                        changed |= node->addPointsTo(NULLPTR);
                    else
                        errorEmptyPointsTo(node, target);
                }

                // we have some pointers - copy them all,
                // since the offset is unknown
                for (auto it : o->pointsTo) {
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
                else
                    errorEmptyPointsTo(node, target);
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

bool PSS::processMemcpy(PSSNode *node)
{
    bool changed = false;

    // what to copy
    std::vector<MemoryObject *> srcObjects;
    // where to copy
    std::vector<MemoryObject *> destObjects;
    PSSNode *srcNode = node->getOperand(0);
    PSSNode *destNode = node->getOperand(1);

    getMemoryObjects(node, srcNode, srcObjects);
    getMemoryObjects(node, destNode, destObjects);

    /* if one is zero initialized and we copy it whole,
     * set the other zero initialized too */
    if ((!destNode->isZeroInitialized() && srcNode->isZeroInitialized())
        && ((*node->offset == 0 && node->len.isUnknown())
            || node->offset.isUnknown())) {
        destNode->setZeroInitialized();
        changed = true;
    }

    if (srcObjects.empty()){
        if (srcNode->isZeroInitialized()) {
            // if the memory is zero initialized,
            // then everything is fine, we add nullptr
            changed |= node->addPointsTo(NULLPTR);
        } else {
            errorEmptyPointsTo(node, srcNode);
        }

        return changed;
    }

    for (MemoryObject *o : destObjects) {
        // copy every pointer from srcObjects that is in
        // the range to these objects
        for (MemoryObject *so : srcObjects) {
            for (auto src : so->pointsTo) {
                // src.first is offset, src.second is a PointToSet

                // we need to copy ptrs at UNKNOWN_OFFSET always
                if (src.first.isUnknown() || node->offset.isUnknown()) {
                    changed |= o->addPointsTo(src.first, src.second);
                    continue;
                }

                if (node->len.isUnknown()) {
                    if (*src.first < *node->offset)
                        continue;
                } else {
                    if (!src.first.inRange(*node->offset,
                                           *node->offset + *node->len - 1))
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
        if (srcNode->isZeroInitialized()
            && !((*node->offset == 0 && node->len.isUnknown())
                 || node->offset.isUnknown()))
            // src is zeroed and we don't copy whole memory?
            changed |= o->addPointsTo(UNKNOWN_OFFSET, NULLPTR);
    }

    return changed;
}

bool PSS::processNode(PSSNode *node)
{
    bool changed = false;
    std::vector<MemoryObject *> objects;

    switch(node->type) {
        case LOAD:
            changed |= processLoad(node);
            break;
        case STORE:
            for (const Pointer& ptr : node->getOperand(1)->pointsTo) {
                PSSNode *target = ptr.target;
                assert(target && "Got nullptr as target");

                if (ptr.isNull())
                    continue;

                objects.clear();
                getMemoryObjects(node, target, objects);
                for (MemoryObject *o : objects) {
                    for (const Pointer& to : node->getOperand(0)->pointsTo)
                        changed |= o->addPointsTo(ptr.offset, to);
                }
            }
            break;
        case GEP:
            for (const Pointer& ptr : node->getOperand(0)->pointsTo) {
                uint64_t new_offset;
                if (ptr.offset.isUnknown() || node->offset.isUnknown())
                    // set it like this to avoid overflow when adding
                    new_offset = UNKNOWN_OFFSET;
                else
                    new_offset = *ptr.offset + *node->offset;

                // in the case the memory has size 0, then every pointer
                // will have unknown offset with the exception that it points
                // to the begining of the memory - therefore make 0 exception
                if (new_offset == 0 || new_offset < ptr.target->getSize())
                    changed |= node->addPointsTo(ptr.target, new_offset);
                else
                    changed |= node->addPointsToUnknownOffset(ptr.target);
            }
            break;
        case CAST:
            // cast only copies the pointers
            for (const Pointer& ptr : node->getOperand(0)->pointsTo)
                changed |= node->addPointsTo(ptr);
            break;
        case CONSTANT:
            // maybe warn? It has no sense to insert the constants into the graph.
            // On the other hand it is harmless. We can at least check if it is
            // correctly initialized 8-)
            assert(node->pointsTo.size() == 1
                   && "Constant should have exactly one pointer");
            break;
        case CALL_RETURN:
        case RETURN:
            // gather pointers returned from subprocedure - the same way
            // as PHI works
        case PHI:
            for (PSSNode *op : node->operands)
                changed |= node->addPointsTo(op->pointsTo);
            break;
        case CALL_FUNCPTR:
            // call via function pointer:
            // first gather the pointers that can be used to the
            // call and if something changes, let backend take some action
            // (for example build relevant subgraph)
            for (const Pointer& ptr : node->getOperand(0)->pointsTo) {
                if (node->addPointsTo(ptr)) {
                    changed = true;

                    if (!ptr.isNull()) {
                        functionPointerCall(node, ptr.target);
                    } else {
                        error(node, "Calling nullptr as a function!");
                        continue;
                    }
                }
            }
            break;
        case MEMCPY:
            changed |= processMemcpy(node);
            break;
        case ALLOC:
        case DYN_ALLOC:
        case FUNCTION:
            // these two always points to itself
            assert(node->doesPointsTo(node, 0));
            assert(node->pointsTo.size() == 1);
        case CALL:
        case ENTRY:
        case NOOP:
            // just no op
            break;
        default:
            assert(0 && "Unknown type");
    }

    return changed;
}

} // namespace pss
} // namespace analysis
} // namespace dg
