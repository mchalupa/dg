#include "llvm/MemAllocationFuncs.h"

namespace dg {

MemAllocationFuncs getMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return MemAllocationFuncs::NONEMEM;

    const char *name = func->getName().data();
    if (strcmp(name, "malloc") == 0)
        return MemAllocationFuncs::MALLOC;
    else if (strcmp(name, "calloc") == 0)
        return MemAllocationFuncs::CALLOC;
    else if (strcmp(name, "alloca") == 0)
        return MemAllocationFuncs::ALLOCA;
    else if (strcmp(name, "realloc") == 0)
        return MemAllocationFuncs::REALLOC;

    return MemAllocationFuncs::NONEMEM;
}
}
