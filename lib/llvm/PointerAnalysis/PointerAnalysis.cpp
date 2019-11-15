// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Instruction.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "llvm/llvm-utils.h"

namespace dg {

std::pair<bool, LLVMMemoryRegionSet>
LLVMPointerAnalysis::getAccessedMemory(const llvm::Instruction *I) {
    using namespace llvm;

    LLVMPointsToSet PTSet;
    LLVMMemoryRegionSet regions;
    Offset len;

    const Module *M = I->getParent()->getParent()->getParent();
    auto& DL = M->getDataLayout();

    if (isa<StoreInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(1));
        len = llvmutils::getAllocatedSize(I->getOperand(0)->getType(), &DL);
    } else if (isa<LoadInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(0));
        len = llvmutils::getAllocatedSize(I->getType(), &DL);
    } else if (auto II = dyn_cast<IntrinsicInst>(I)) {
        switch(II->getIntrinsicID()) {
            // lifetime start/end do not access the memory,
            // but the user may want to find out information
            // also about these
            case Intrinsic::lifetime_start:
            case Intrinsic::lifetime_end:
                PTSet = getLLVMPointsTo(I->getOperand(1));
                len = llvmutils::getConstantValue(II->getOperand(0));
                break;
            default:
                llvm::errs() << "ERROR: Unhandled intrinsic\n"
                             << *I << "\n";
                return {true, regions};
        }
    } else {
        if (I->mayReadOrWriteMemory()) {
            llvm::errs() << "[ERROR]: getAccessedMemory: unhandled intruction\n"
                         << *I << "\n";
        } else {
            llvm::errs() << "[ERROR]: getAccessedMemory: queries invalid instruction\n"
                         << *I << "\n";
        }
        return {true, regions};
    }

    if (len == 0) // unknown length
        len = Offset::UNKNOWN;

    // translate to regions
    for (const auto& ptr : PTSet) {
        regions.add(ptr.value, ptr.offset, len);
    }

    return {PTSet.hasUnknown(), regions};
}

} // namespace dg
