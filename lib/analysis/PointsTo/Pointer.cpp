#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/PSNode.h"

#ifndef NDEBUG
#include <iostream>

namespace dg {
namespace analysis {
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
} // namespace analysis
} // namespace dg

#endif // not NDEBUG

