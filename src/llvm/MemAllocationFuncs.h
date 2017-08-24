#ifndef _DG_MEMALLOCATIONFUNCS_H_
#define _DG_MEMALLOCATIONFUNCS_H_

#include <llvm/IR/Instructions.h>

namespace dg {

enum class MemAllocationFuncs {
    NONEMEM,
    MALLOC,
    CALLOC,
    ALLOCA,
    REALLOC,
};

MemAllocationFuncs getMemAllocationFunc(const llvm::Function *func);
}


#endif // _DG_MEMALLOCATIONFUNCS_H_
