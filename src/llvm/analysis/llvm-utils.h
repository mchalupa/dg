#ifndef _DG_LLVM_UTILS_H_
#define _DG_LLVM_UTILS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
//#include <llvm/IR/DataLayout.h>

namespace dg {
namespace llvmutils {

inline bool isPointerOrIntegerTy(const llvm::Type *Ty)
{
    return Ty->isPointerTy() || Ty->isIntegerTy();
}

// can given function be called by the call inst?
inline bool callIsCompatible(const llvm::Function *F, const llvm::CallInst *CI)
{
    using namespace llvm;

    if (F->isVarArg()) {
        if (F->arg_size() > CI->getNumArgOperands())
            return false;
    } else {
        if (F->arg_size() != CI->getNumArgOperands())
            return false;
    }

    if (!F->getReturnType()->canLosslesslyBitCastTo(CI->getType()))
        // it showed up that the loosless bitcast is too strict
        // alternative since we can use the constexpr castings
        if (!(isPointerOrIntegerTy(F->getReturnType()) && isPointerOrIntegerTy(CI->getType())))
            return false;

    int idx = 0;
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
        Type *CTy = CI->getArgOperand(idx)->getType();
        Type *ATy = A->getType();

        if (!(isPointerOrIntegerTy(CTy) && isPointerOrIntegerTy(ATy)))
            if (!CTy->canLosslesslyBitCastTo(ATy))
                return false;
    }

    return true;
}

} // namespace llvmutils
} // namespace dg

#endif //  _DG_LLVM_UTILS_H_

