#ifndef _DG_MEMALLOCATIONFUNCS_H_
#define _DG_MEMALLOCATIONFUNCS_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Function.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

namespace dg {

enum class MemAllocationFuncs {
    NONEMEM,
    MALLOC,
    CALLOC,
    ALLOCA,
    REALLOC,
};

inline MemAllocationFuncs getMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return MemAllocationFuncs::NONEMEM;

    const auto& name = func->getName();
    if (name.equals("malloc"))
        return MemAllocationFuncs::MALLOC;
    else if (name.equals("calloc"))
        return MemAllocationFuncs::CALLOC;
    else if (name.equals("alloca"))
        return MemAllocationFuncs::ALLOCA;
    else if (name.equals("realloc"))
        return MemAllocationFuncs::REALLOC;

    return MemAllocationFuncs::NONEMEM;
}

} // namespace dg



#endif // _DG_MEMALLOCATIONFUNCS_H_
