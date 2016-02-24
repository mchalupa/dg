#include "Pointer.h"
#include "PSS.h"

namespace dg {
namespace analysis {

// nodes representing NULL and unknown memory
PSSNode NULLPTR_LOC(pss::NULLPTR);
PSSNode *NULLPTR = &NULLPTR_LOC;
PSSNode UNKNOWN_MEMLOC(pss::UNKNOWN_MEMLOC);
PSSNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

// pointers to those memory
const Pointer PointerUnknown(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
const Pointer PointerNull(NULLPTR, 0);

using namespace pss;

// replace all pointers to given target with one
// to that target, but UNKNOWN_OFFSET
bool PSSNode::addPointsToUnknownOffset(PSSNode *target)
{
    bool changed = false;
    // FIXME: use equal range, it is much faster
    for (auto I = pointsTo.begin(), E = pointsTo.end(); I != E;) {
        auto cur = I++;
        if (cur->target == target && !cur->offset.isUnknown()) {
            pointsTo.erase(cur);
            changed = true;
        }
    }

    changed |= addPointsTo(target, UNKNOWN_OFFSET);
    return changed;
}

bool PSS::processNode(PSSNode *node)
{
    bool changed = false;
    std::vector<MemoryObject *> objects;

    switch(node->type) {
        case ALLOC:
        case DYN_ALLOC:
            changed |= node->addPointsTo(node, 0);
            break;
        case LOAD:
            for (const Pointer& ptr : node->getOperand(0)->pointsTo) {
                PSSNode *target = ptr.target;
                assert(target && "Got nullptr as target");

                objects.clear();
                getMemoryObjects(node, target, objects);

                for (MemoryObject *o : objects) {
                    for (const Pointer& memptr : o->pointsTo[ptr.offset]) {
                        changed |= node->addPointsTo(memptr);
                    }
                }
            }
            break;
        case STORE:
            for (const Pointer& ptr : node->getOperand(1)->pointsTo) {
                PSSNode *target = ptr.target;
                assert(target && "Got nullptr as target");

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
                uint64_t new_offset = *ptr.offset + *node->offset;
                // in the case the memory has size 0, then every pointer
                // will have unknown offset
                if (new_offset < ptr.target->getSize())
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
        case CALL:
        case RETURN:
            break;
        case PHI:
            for (PSSNode *op : node->operands)
                changed |= node->addPointsTo(op->pointsTo);
            break;
        default:
            assert(0 && "Unknown type");
    }

    return changed;
}

} // namespace analysis
} // namespace dg
