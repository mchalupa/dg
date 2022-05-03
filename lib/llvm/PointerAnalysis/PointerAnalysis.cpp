#include <llvm/IR/Instruction.h>

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "llvm/llvm-utils.h"

namespace dg {

std::pair<bool, LLVMMemoryRegionSet>
LLVMPointerAnalysis::getAccessedMemory(const llvm::Instruction *I) {
    using namespace llvm;

    LLVMPointsToSet PTSet;
    LLVMPointsToSet PTSet2; // memmove and such use two ptsets
    LLVMMemoryRegionSet regions;
    Offset len;

    const Module *M = I->getParent()->getParent()->getParent();
    const auto &DL = M->getDataLayout();

    if (isa<StoreInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(1));
        len = llvmutils::getAllocatedSize(I->getOperand(0)->getType(), &DL);
    } else if (isa<LoadInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(0));
        len = llvmutils::getAllocatedSize(I->getType(), &DL);
    } else if (isa<VAArgInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(0));
        len = Offset::UNKNOWN;
    } else if (isa<AtomicCmpXchgInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(0));
        len = llvmutils::getAllocatedSize(I->getOperand(2)->getType(), &DL);
    } else if (isa<AtomicRMWInst>(I)) {
        PTSet = getLLVMPointsTo(I->getOperand(0));
        len = llvmutils::getAllocatedSize(I->getOperand(1)->getType(), &DL);
    } else if (const auto *II = dyn_cast<IntrinsicInst>(I)) {
        switch (II->getIntrinsicID()) {
        // lifetime start/end do not access the memory,
        // but the user may want to find out information
        // also about these
        case Intrinsic::lifetime_start:
        case Intrinsic::lifetime_end:
            PTSet = getLLVMPointsTo(I->getOperand(1));
            len = llvmutils::getConstantValue(II->getOperand(0));
            break;
        case Intrinsic::memset:
            PTSet = getLLVMPointsTo(I->getOperand(0));
            len = llvmutils::getConstantValue(II->getOperand(2));
            break;
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            PTSet = getLLVMPointsTo(I->getOperand(0));
            PTSet2 = getLLVMPointsTo(I->getOperand(1));
            len = llvmutils::getConstantValue(II->getOperand(2));
            break;
        case Intrinsic::vastart:
        case Intrinsic::vaend:
            PTSet = getLLVMPointsTo(I->getOperand(0));
            len = Offset::UNKNOWN;
            break;
        case Intrinsic::vacopy:
            PTSet = getLLVMPointsTo(I->getOperand(0));
            PTSet2 = getLLVMPointsTo(I->getOperand(1));
            len = Offset::UNKNOWN;
            break;
        default:
            llvm::errs() << "ERROR: Unhandled intrinsic\n" << *I << "\n";
            return {true, regions};
        }
    } else if (const auto *CI = dyn_cast<CallInst>(I)) {
        if (CI->doesNotAccessMemory()) {
            return {false, regions};
        }

        // check which operands are pointers and get the information for them
        bool hasUnknown = false;
        for (const auto &arg : llvmutils::args(CI)) {
            if (hasPointsTo(arg)) {
                auto tmp = getLLVMPointsToChecked(arg);
                hasUnknown |= tmp.first;
                // translate to regions
                for (const auto &ptr : tmp.second) {
                    regions.add(ptr.value, Offset::UNKNOWN, Offset::UNKNOWN);
                }
            }
        }
        return {hasUnknown, regions};
    } else {
        if (I->mayReadOrWriteMemory()) {
            llvm::errs() << "[ERROR]: getAccessedMemory: unhandled intruction\n"
                         << *I << "\n";
        } else {
            llvm::errs() << "[ERROR]: getAccessedMemory: queries invalid "
                            "instruction\n"
                         << *I << "\n";
        }
        return {true, regions};
    }

    if (len == 0) // unknown length
        len = Offset::UNKNOWN;

    // translate to regions
    for (const auto &ptr : PTSet) {
        regions.add(ptr.value, ptr.offset, len);
    }
    for (const auto &ptr : PTSet2) {
        regions.add(ptr.value, ptr.offset, len);
    }

    return {PTSet.hasUnknown(), regions};
}

} // namespace dg
