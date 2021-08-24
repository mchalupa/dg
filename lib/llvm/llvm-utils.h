#ifndef DG_LLVM_UTILS_H_
#define DG_LLVM_UTILS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IntrinsicInst.h>


#include "dg/Offset.h"

namespace dg {
namespace llvmutils {

using namespace llvm;

/* ----------------------------------------------
 * -- PRINTING
 * ---------------------------------------------- */
inline void print(const Value *val, raw_ostream &os,
                  const char *prefix = nullptr, bool newline = false) {
    if (prefix)
        os << prefix;

    if (isa<Function>(val))
        os << val->getName().data();
    else
        os << *val;

    if (newline)
        os << "\n";
}

inline void printerr(const char *msg, const Value *val, bool newline = true) {
    print(val, errs(), msg, newline);
}

/* ----------------------------------------------
 * -- CASTING
 * ---------------------------------------------- */
inline bool isPointerOrIntegerTy(const Type *Ty) {
    return Ty->isPointerTy() || Ty->isIntegerTy();
}

// can the given function be called by the given call inst?
enum class CallCompatibility {
    STRICT,       // require full compatibility
    LOOSE,        // ignore some incompatible patterns that usually work
                  // in practice, e.g., calling a function of 2 arguments
                  // with 3 arguments.
    MATCHING_ARGS // check only that matching arguments are compatible,
                  // ignore the number of arguments, etc.
};

inline bool
callIsCompatible(const Function *F, const CallInst *CI,
                 CallCompatibility policy = CallCompatibility::LOOSE) {
    using namespace llvm;

    if (policy != CallCompatibility::MATCHING_ARGS) {
        if (F->isVarArg()) {
            if (F->arg_size() > CI->getNumArgOperands()) {
                return false;
            }
        } else if (F->arg_size() != CI->getNumArgOperands()) {
            if (policy == CallCompatibility::STRICT ||
                F->arg_size() > CI->getNumArgOperands()) {
                // too few arguments
                return false;
            }
        }

        if (!F->getReturnType()->canLosslesslyBitCastTo(CI->getType())) {
            // it showed up that the loosless bitcast is too strict
            // alternative since we can use the constexpr castings
            if (!(isPointerOrIntegerTy(F->getReturnType()) &&
                  isPointerOrIntegerTy(CI->getType()))) {
                return false;
            }
        }
    }

    size_t idx = 0;
    auto max_idx = CI->getNumArgOperands();
    for (auto A = F->arg_begin(), E = F->arg_end(); idx < max_idx && A != E;
         ++A, ++idx) {
        Type *CTy = CI->getArgOperand(idx)->getType();
        Type *ATy = A->getType();

        if (!(isPointerOrIntegerTy(CTy) && isPointerOrIntegerTy(ATy)))
            if (!CTy->canLosslesslyBitCastTo(ATy)) {
                return false;
            }
    }

    return true;
}

/* ----------------------------------------------
 * -- analysis helpers
 * ---------------------------------------------- */

inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                   const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

inline uint64_t getConstantValue(const llvm::Value *op) {
    using namespace llvm;

    // FIXME: we should get rid of this dependency
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
                                 const llvm::DataLayout *DL) {
    llvm::Type *Ty = AI->getAllocatedType();
    if (!Ty->isSized())
        return 0;

    if (AI->isArrayAllocation()) {
        return getConstantSizeValue(AI->getArraySize()) *
               DL->getTypeAllocSize(Ty);
    }
    return DL->getTypeAllocSize(Ty);
}

inline uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL) {
    // Type can be i8 *null or similar
    if (!Ty->isSized())
        return 0;

    return DL->getTypeAllocSize(Ty);
}

inline bool isConstantZero(const llvm::Value *val) {
    using namespace llvm;

    if (const ConstantInt *C = dyn_cast<ConstantInt>(val))
        return C->isZero();

    return false;
}

/* ----------------------------------------------
 * -- pointer analysis helpers
 * ---------------------------------------------- */
inline bool memsetIsZeroInitialization(const llvm::IntrinsicInst *I) {
    return isConstantZero(I->getOperand(1));
}

// recursively find out if type contains a pointer type as a subtype
// (or if it is a pointer type itself)
inline bool tyContainsPointer(const llvm::Type *Ty) {
    if (Ty->isAggregateType()) {
        for (auto I = Ty->subtype_begin(), E = Ty->subtype_end(); I != E; ++I) {
            if (tyContainsPointer(*I))
                return true;
        }
    } else
        return Ty->isPointerTy();

    return false;
}

inline bool typeCanBePointer(const llvm::DataLayout *DL, llvm::Type *Ty) {
    if (Ty->isPointerTy())
        return true;

    if (Ty->isIntegerTy() && Ty->isSized())
        return DL->getTypeSizeInBits(Ty) >=
               DL->getPointerSizeInBits(/*Ty->getPointerAddressSpace()*/);

    return false;
}

template <typename ItTy, typename value_type>
class use_iterator_impl {
    ItTy it;

public:
      explicit use_iterator_impl(ItTy i) : it(i) {}

      using iterator_category = std::forward_iterator_tag;
     using difference_type = std::ptrdiff_t;
     using pointer = value_type *;
     using reference = value_type &;

     use_iterator_impl() = default;

     bool operator==(const use_iterator_impl &x) const { return it == x.it; }
     bool operator!=(const use_iterator_impl &x) const { return !operator==(x); }

     use_iterator_impl &operator++() { // Preincrement
       ++it;
       return *this;
     }

     use_iterator_impl operator++(int) { // Postincrement
       auto tmp = *this;
       ++*this;
       return tmp;
     }

     value_type *operator*() const {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
    return *it;
#else
    return it->getUser();
#endif
  }

  value_type *operator->() const { return operator*(); }
};

using use_iterator = use_iterator_impl<llvm::Value::use_iterator, llvm::Value>;
using const_use_iterator = use_iterator_impl<llvm::Value::const_use_iterator,
                                             const llvm::Value>;

inline llvm::iterator_range<use_iterator> uses(llvm::Value *val) {
    return llvm::make_range(val->use_begin(), val->use_end());
}

inline llvm::iterator_range<const_use_iterator> uses(const llvm::Value *val) {
    return llvm::make_range(val->use_begin(), val->use_end());
}

namespace {
    template <typename FunTy, typename CallTy>
    inline std::vector<CallTy *> calls_of(FunTy *fun) {
        using namespace llvm;

        std::vector<CallTy *> calls;
        for (auto *use : uses(fun)) {
            if (auto *CI = dyn_cast<CallInst>(use)) {
                calls.push_back(CI);
            } else if (auto *BC = dyn_cast<BitCastInst>(use)) {
                // the use of call is bitcased
                for (auto *bcuse : uses(use)) {
                    assert(bcuse && "Invalid use");
                    // the use must by call inst, otherwise abort
                    if (auto *CI = dyn_cast<CallInst>(bcuse)) {
                        if (CI->getCalledOperand()->stripPointerCasts() == fun) {
                            // is the falled function 'fun'? (fun can be also
                            // an argument)
                            calls.push_back(CI);
                        }
                    } else {
                        printerr("Unknown use of function", use);
                        assert(false && "Unknown use of function");
                    }
                }
            } else if (auto *CE = dyn_cast<ConstantExpr>(use)) {
                if (!CE->isCast()) {
                    printerr("Unknown use of function", use);
                    assert(false && "Unknown use of function");
                }
                // the use of call is bitcased
                for (auto *bcuse : uses(CE)) {
                    assert(bcuse && "Invalid use");
                    // the use must by call inst, otherwise abort
                    if (auto *CI = dyn_cast<CallInst>(bcuse)) {
                        if (CI->getCalledOperand()->stripPointerCasts() == fun) {
                            calls.push_back(CI);
                        }
                    } else {
                        printerr("Unknown use of function", use);
                        assert(false && "Unknown use of function");
                    }
                }
            } else if (!isa<StoreInst>(use)) {
                printerr("Unknown use of function", use);
                assert(false && "Unknown use of function");
            }
        }

        return calls;
    }
}

inline std::vector<llvm::CallInst *> calls_of(llvm::Function *fun) {
    return calls_of<llvm::Function, llvm::CallInst>(fun);
}

inline std::vector<const llvm::CallInst *> calls_of(const llvm::Function *fun) {
    return calls_of<const llvm::Function, const llvm::CallInst>(fun);
}

///
// A wrapper around CallInst that provides a unified API
// for different versions of LLVM and some auxiliary methods.
// Somethig as AbstractCallInst in newer LLVMs.
class CallInstInfo {
    const llvm::CallInst *call;

public:
    CallInstInfo(const llvm::CallInst *CI) : call(CI) {}

    const llvm::Value *getCalledValue() const {
#if LLVM_VERSION_MAJOR >= 8
        return call->getCalledOperand();
#else
        return call->getCalledValue();
#endif
    }

    const llvm::Value *getCalledStrippedValue() const {
        return getCalledValue()->stripPointerCasts();
    }

};

} // namespace llvmutils
} // namespace dg

#endif //  DG_LLVM_UTILS_H_
