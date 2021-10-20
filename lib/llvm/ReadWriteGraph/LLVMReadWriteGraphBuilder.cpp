#include <cassert>
#include <set>

#include <llvm/Config/llvm-config.h>
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/Support/CFG.h>
#else
#include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include <llvm/IR/Dominators.h>

#include "dg/ADT/Queue.h"
#include "dg/llvm/PointerAnalysis/PointerGraph.h"

#include "llvm/ForkJoin/ForkJoin.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const ValInfo &vi) {
    using namespace llvm;

    if (const auto *I = dyn_cast<Instruction>(vi.v)) {
        os << I->getParent()->getParent()->getName();
        if (auto &DL = I->getDebugLoc()) {
            os << " (line " << DL.getLine() << ", col " << DL.getCol() << ")";
        }
        os << " :: " << *I;
    } else if (const auto *A = dyn_cast<Argument>(vi.v)) {
        os << A->getParent()->getParent()->getName() << ":: (arg) " << *A;
    } else if (const auto *F = dyn_cast<Function>(vi.v)) {
        os << "(func) " << F->getName();
    } else {
        os << *vi.v;
    }

    return os;
}

///
// Map pointers of 'val' to def-sites.
// \param where  location in the program, for debugging
// \param size is the number of bytes used from the memory
std::vector<DefSite>
LLVMReadWriteGraphBuilder::mapPointers(const llvm::Value *where,
                                       const llvm::Value *val, Offset size) {
    std::vector<DefSite> result;

    auto psn = PTA->getLLVMPointsToChecked(val);
    if (!psn.first) {
        result.emplace_back(UNKNOWN_MEMORY);
#ifndef NDEBUG
        llvm::errs() << "[RWG] warning at: " << ValInfo(where) << "\n";
        llvm::errs() << "No points-to set for: " << ValInfo(val) << "\n";
#endif
        // don't have points-to information for used pointer
        return result;
    }

    if (psn.second.empty()) {
#ifndef NDEBUG
        llvm::errs() << "[RWG] warning at: " << ValInfo(where) << "\n";
        llvm::errs() << "Empty points-to set for: " << ValInfo(val) << "\n";
#endif
        // this may happen on invalid reads and writes to memory,
        // like when you try for example this:
        //
        //   int p, q;
        //   memcpy(p, q, sizeof p);
        //
        // (there should be &p and &q)
        // NOTE: maybe this is a bit strong to say unknown memory,
        // but better be sound then incorrect
        result.emplace_back(UNKNOWN_MEMORY);
        return result;
    }

    result.reserve(psn.second.size());

    if (psn.second.hasUnknown()) {
        result.emplace_back(UNKNOWN_MEMORY);
    }

    for (const auto &ptr : psn.second) {
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        RWNode *ptrNode = getOperand(ptr.value);
        if (!ptrNode) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RWG] error at " << ValInfo(where) << "\n";
                llvm::errs() << "[RWG] error for " << ValInfo(val) << "\n";
                llvm::errs() << "[RWG] error: Cannot find node for "
                             << ValInfo(ptr.value) << "\n";
            }
            continue;
        }

        // FIXME: we should pass just size to the DefSite ctor, but the old code
        // relies on the behavior that when offset is unknown, the length is
        // also unknown. So for now, mimic the old code. Remove it once we fix
        // the old code.
        result.emplace_back(ptrNode, ptr.offset,
                            ptr.offset.isUnknown() ? Offset::UNKNOWN : size);
    }

    return result;
}

RWNode *LLVMReadWriteGraphBuilder::getOperand(const llvm::Value *val) {
    auto *op = getNode(val);
    if (!op) {
        // lazily create allocations as these are targets in defsites
        // and may not have been created yet
        if (llvm::isa<llvm::AllocaInst>(val) ||
            // FIXME: check that it is allocation
            llvm::isa<llvm::CallInst>(val)) {
            op = buildNode(val).getRepresentant();
        }

        if (!op) {
            llvm::errs() << "[RWG] error: cannot find an operand: " << *val
                         << "\n";
            abort();
        }
    }
    assert(op && "Do not have an operand");
    return op;
}

} // namespace dda
} // namespace dg
