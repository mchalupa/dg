#include <assert.h>

#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PSNode.h"

namespace dg {
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
const Pointer UnknownPointer(UNKNOWN_MEMORY, Offset::UNKNOWN);
const Pointer NullPointer(NULLPTR, 0);

void PointerGraph::remove(PSNode *nd) {
    assert(nd && "nullptr passed as nd");
    // the node must be isolated
    assert(nd->successors().empty() && "The node is still in graph");
    assert(nd->predecessors().empty() && "The node is still in graph");
    assert(nd->getID() < size() && "Invalid ID");
    assert(nd->getID() > 0 && "Invalid ID");
    assert(nd->users.empty() && "This node is used by other nodes");
    // if the node has operands, it means that the operands
    // have a reference (an user edge to this node).
    // We do not want to create dangling references.
    assert(nd->operands.empty() && "This node uses other nodes");
    assert(nodes[nd->getID()].get() == nd && "Inconsistency in nodes");

    // clear the nodes entry
    nodes[nd->getID()].reset();
}



} // namespace pta
} // namespace dg
