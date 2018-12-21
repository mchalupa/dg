#ifndef _DG_LLVM_UTILS_H_
#define _DG_LLVM_UTILS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Instructions.h>

namespace dg {
namespace llvmutils {

using namespace llvm;

/* ----------------------------------------------
 * -- PRINTING
 * ---------------------------------------------- */
inline void print(const Value *val,
                  raw_ostream& os,
                  const char *prefix=nullptr,
                  bool newline = false)
{
    if (prefix)
        os << prefix;

    if (isa<Function>(val))
        os << val->getName().data();
    else
        os << *val;

    if (newline)
        os << "\n";
}

inline void printerr(const char *msg, const Value *val, bool newline = true)
{
    print(val, errs(), msg, newline);
}

/* ----------------------------------------------
 * -- CASTING
 * ---------------------------------------------- */
inline bool isPointerOrIntegerTy(const Type *Ty)
{
    return Ty->isPointerTy() || Ty->isIntegerTy();
}

// can the given function be called by the given call inst?
inline bool callIsCompatible(const Function *F, const CallInst *CI)
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

/* ----------------------------------------------
 * -- analysis helpers
 * ---------------------------------------------- */
namespace analysis {

inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}



inline uint64_t getConstantValue(const llvm::Value *op)
{
    using namespace llvm;

    //FIXME: we should get rid of this dependency
    static_assert(sizeof(Offset::type) == sizeof(uint64_t),
                  "The code relies on Offset::type having 8 bytes");

    uint64_t size = Offset::UNKNOWN;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
    }

    // size is ~((uint64_t)0) if it is unknown
    return size;
}


// get size of memory allocation argument
inline uint64_t getConstantSizeValue(const llvm::Value *op) {
    auto sz = getConstantValue(op);
    // if the size is unknown, make it 0, so that pointer
    // analysis correctly computes offets into this memory
    // (which is always UNKNOWN)
    if (sz == ~static_cast<uint64_t>(0))
        return 0;
    return sz;
}

inline uint64_t getAllocatedSize(const llvm::AllocaInst *AI,
                                 const llvm::DataLayout *DL)
{
    llvm::Type *Ty = AI->getAllocatedType();
    if (!Ty->isSized())
            return 0;

    if (AI->isArrayAllocation()) {
        return getConstantSizeValue(AI->getArraySize()) * DL->getTypeAllocSize(Ty);
    } else
        return DL->getTypeAllocSize(Ty);
}

inline uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return 0;

    return DL->getTypeAllocSize(Ty);
}

inline bool isConstantZero(const llvm::Value *val)
{
    using namespace llvm;

    if (const ConstantInt *C = dyn_cast<ConstantInt>(val))
        return C->isZero();

    return false;
}

/* ----------------------------------------------
 * -- pointer analysis helpers
 * ---------------------------------------------- */
namespace pta {
inline bool memsetIsZeroInitialization(const llvm::IntrinsicInst *I)
{
    return isConstantZero(I->getOperand(1));
}

// recursively find out if type contains a pointer type as a subtype
// (or if it is a pointer type itself)
inline bool tyContainsPointer(const llvm::Type *Ty)
{
    if (Ty->isAggregateType()) {
        for (auto I = Ty->subtype_begin(), E = Ty->subtype_end();
             I != E; ++I) {
            if (tyContainsPointer(*I))
                return true;
        }
    } else
        return Ty->isPointerTy();

    return false;
}

inline bool typeCanBePointer(const llvm::DataLayout *DL, llvm::Type *Ty)
{
    if (Ty->isPointerTy())
        return true;

    if (Ty->isIntegerTy() && Ty->isSized())
        return DL->getTypeSizeInBits(Ty)
                >= DL->getPointerSizeInBits(/*Ty->getPointerAddressSpace()*/);

    return false;
}

} // namespace pta

} // namespace analysis
} // namespace dg

#endif //  _DG_LLVM_UTILS_H_

