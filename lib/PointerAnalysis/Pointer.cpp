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


} // namespace pta
} // namespace dg

#endif // not NDEBUG

