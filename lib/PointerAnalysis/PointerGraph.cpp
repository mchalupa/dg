#include "dg/PointerAnalysis/PointerGraph.h"

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

} // namespace pta
} // namespace dg
