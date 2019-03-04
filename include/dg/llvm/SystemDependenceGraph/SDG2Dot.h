#ifndef DG_LLVM_SDG2DOT_H_
#define DG_LLVM_SDG2DOT_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "llvm/IR/Instructions.h"

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 4))
#include "llvm/DebugInfo.h"     //DIScope
#else
#include "llvm/IR/DebugInfo.h"     //DIScope
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <sstream>

#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"

namespace dg {
namespace llvmdg {

/*
FIXME: move to one file...
static std::ostream& operator<<(std::ostream& os, const analysis::Offset& off)
{
    if (off.offset == Offset::UNKNOWN)
        os << "UNKNOWN";
    else
        os << off.offset;

    return os;
}
*/

class SDG2Dot {
public:
    SDG2Dot(SystemDependenceGraph *sdg, const std::string& file) {
        assert(false && "Not implemented yet");
    }

    void dump() const {
        assert(false && "Not implemented yet");
    }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SDG2DOT_H_
