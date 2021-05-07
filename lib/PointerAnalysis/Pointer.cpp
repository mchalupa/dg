#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PSNode.h"

#ifndef NDEBUG
#include <iostream>

namespace dg {
namespace pta {

void Pointer::dump() const {
    target->dump();
    std::cout << " + ";
    offset.dump();
}

void Pointer::print() const {
    dump();
    std::cout << "\n";
}

size_t Pointer::hash() const {
    static_assert(sizeof(size_t) == 8, "We relay on 64-bit size_t");

    // we relay on the fact the offsets are usually small. Therefore,
    // cropping them to 4 bytes and putting them together with ID (which is 4
    // byte) into one uint64_t should not have much collisions... we'll see.
    constexpr unsigned mask = 0xffffffff;
    constexpr unsigned short shift = 32;
    return (static_cast<uint64_t>(target->getID()) << shift) | (*offset & mask);
}

} // namespace pta
} // namespace dg

#endif // not NDEBUG
