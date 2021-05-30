#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>

#include "dg/llvm/PointerAnalysis/AliasAnalysis.h"

namespace dg {
namespace llvmdg {

using namespace llvm;

/////
// Auxiliary functions
//////

static uint64_t getAllocatedSize(llvm::Type *Ty,
                                 const llvm::DataLayout &DL) {
    if (!Ty->isSized())
        return 0;
    return DL.getTypeAllocSize(Ty);
}

static std::pair<const Value *, unsigned>
getAccessedMemory(const Instruction *I, const DataLayout& DL) {
    if (auto *S = dyn_cast<StoreInst>(I)) {
        return {S->getPointerOperand(),
                getAllocatedSize(S->getValueOperand()->getType(), DL)};
    } else if (auto *L = dyn_cast<LoadInst>(I)) {
       return {L->getPointerOperand(),
                getAllocatedSize(L->getType(), DL)};
    }
    return {nullptr, 0};
}

static bool hasAddressTaken(const AllocaInst *AI) {
    // check that this alloca is used only for loading and storing
    // information
    // FIXME: very imprecise -- we should check if the address
    // of the alloca is stored somewhere. However, in that case
    // we must track also geps, bitcasts, ptrtoints, etc... of
    // this AI and check whether these are not stored.
    for (auto I = AI->use_begin(), E = AI->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        auto *use = *I;
#else
        auto *use = I->getUser();
#endif
        if (auto *S = dyn_cast<StoreInst>(use)) {
            if (S->getPointerOperand() == AI &&
                S->getValueOperand() != AI) {
                continue;
            }
        }
        if (auto *L = dyn_cast<LoadInst>(use)) {
            if (L->getPointerOperand() == AI) {
                continue;
            }
        }
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////////

BasicLLVMAliasAnalysis::BasicLLVMAliasAnalysis(const llvm::Module& M)
: LLVMAliasAnalysis(M), DL(M.getDataLayout()) {}

///
// May 'v1' and 'v2' reference the same byte in memory?
AliasResult BasicLLVMAliasAnalysis::alias(const Value *v1,
                                          const Value *v2) {
    if (v1 == v2)
        return AliasResult::MUST;

    v1 = v1->stripPointerCasts();
    v2 = v2->stripPointerCasts();
    if (isa<AllocaInst>(v1) && isa<AllocaInst>(v2) && v1 != v2)
        return AliasResult::NO;
    return AliasResult::MAY;
}

///
// May accessing 'b1' bytes via pointer 'v1' and 'b2' bytes via 'v2'
// access a same byte in memory?
AliasResult BasicLLVMAliasAnalysis::access(const llvm::Value *v1,
                                           const llvm::Value *v2,
                                           unsigned int /* b1 */,
                                           unsigned int /* b2 */) {
    if (v1 == v2)
        return AliasResult::MUST;
    return alias(v1, v2);
}

///
// May the two instructions access the same byte in memory?
AliasResult BasicLLVMAliasAnalysis::access(const llvm::Instruction *I1,
                                           const llvm::Instruction *I2) {
    const Value *ptr1, *ptr2;
    unsigned int bytes1, bytes2;
    std::tie(ptr1, bytes1) = getAccessedMemory(I1, DL);
    std::tie(ptr2, bytes2) = getAccessedMemory(I2, DL);

    if (ptr1 && ptr2 && bytes1 > 0 && bytes2 > 0)
        return access(ptr1, ptr2, bytes1, bytes2);

    return AliasResult::MAY;
}

///
// Are 'b1' bytes beginning with 'v1' a superset (supsequence) of
// 'b2' bytes starting from 'v2'?
AliasResult BasicLLVMAliasAnalysis::covers(const llvm::Value *v1,
                                           const llvm::Value *v2,
                                           unsigned int b1,
                                           unsigned int b2) {
    if (b1 < b2)
        return AliasResult::NO;

    if (v1 == v2)
        return AliasResult::MUST;

    auto *A1 = dyn_cast<AllocaInst>(v1);
    auto *A2 = dyn_cast<AllocaInst>(v2);
    if (A1 && A2) {
        if (A1 != A2)
            return AliasResult::NO;
        // A1 == A2
        if (!hasAddressTaken(A1))
            return AliasResult::MUST;
    }
    return AliasResult::MAY;
}

///
// Does instruction I1 access all the bytes accessed by I2?
AliasResult BasicLLVMAliasAnalysis::covers(const llvm::Instruction *I1,
                                           const llvm::Instruction *I2) {
    const Value *ptr1, *ptr2;
    unsigned int bytes1, bytes2;
    std::tie(ptr1, bytes1) = getAccessedMemory(I1, DL);
    std::tie(ptr2, bytes2) = getAccessedMemory(I2, DL);

    if (ptr1 && ptr2 && bytes1 > 0 && bytes2 > 0)
        return covers(ptr1, ptr2, bytes1, bytes2);

    return AliasResult::MAY;
}

} // namespace llvmdg
} // namespace dg
