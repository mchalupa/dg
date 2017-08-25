#include "llvm/MemAllocationFuncs.h"

#include <llvm/IR/Function.h>

namespace dg {

MemAllocationFuncs getMemAllocationFunc(const llvm::Function *func)
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
}
