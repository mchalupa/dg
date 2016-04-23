#include <cassert>
#include "Pointer.h"

namespace dg {
namespace analysis {

#if 0
// pointer points to unknown memory location
// we don't know the size of unknown memory location
MemoryObject UnknownMemoryObject(~((uint64_t) 0));
// dereferencing null pointer is undefined behaviour,
// so it's nice to keep track of that - again we can
// write to null with any offset
MemoryObject NullMemoryObject(~((uint64_t) 0));
// unknown pointer value
Pointer UnknownMemoryLocation(&UnknownMemoryObject, 0);
Pointer NullPointer(&NullMemoryObject, 0);

bool Pointer::isUnknown() const
{
    return this == &UnknownMemoryLocation;
}

bool Pointer::isNull() const
{
    return object->isNull();
}

bool Pointer::pointsToUnknown() const
{
    assert(object && "Pointer has not any memory object set");
    return object->isUnknown();
}

bool Pointer::isKnown() const
{
    return !isUnknown() && !pointsToUnknown() &&!isNull();
}

bool Pointer::pointsToHeap() const
{
    assert(object && "Pointer has not any memory object set");
    // XXX what about unknown pointers?
    return object->isHeapAllocated();
}

bool MemoryObject::isNull() const
{
    return this == &NullMemoryObject;
}

bool MemoryObject::isUnknown() const
{
    return this == &UnknownMemoryObject;
}
#endif

} // namespace analysis
} // namespace dg

