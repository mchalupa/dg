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
                changed |= node->addPointsTo(ptr.target,
                                             *ptr.offset + *node->offset);
            }
            break;
        case CONSTANT:
            // maybe warn? It has no sense to insert the constants into the graph.
            // On the other hand it is harmless. We can at least check if it is
            // correctly initialized 8-)
            assert(node->pointsTo.size() == 1
                   && "Constant should have exactly one pointer");
            break;
        case PHI:
        case CALL:
        case RETURN:
            assert(0 && "Not implemented yet");
            break;
        default:
            assert(0 && "Unknown type");
    }

    return changed;
}

} // namespace analysis
} // namespace dg
