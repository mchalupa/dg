#include <set>
#include <cassert>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Config/llvm-config.h>
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_os_ostream.h>

#include <llvm/IR/Dominators.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/PointerAnalysis/PointerGraph.h"
#include "dg/ADT/Queue.h"

#include "llvm/ForkJoin/ForkJoin.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const ValInfo& vi) {
    using namespace llvm;

    if (auto I = dyn_cast<Instruction>(vi.v)) {
        os << I->getParent()->getParent()->getName()
               << ":: " << *I;
    } else if (auto A = dyn_cast<Argument>(vi.v)) {
        os << A->getParent()->getParent()->getName()
               << ":: (arg) " << *A;
    } else if (auto F = dyn_cast<Function>(vi.v)) {
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
                                       const llvm::Value *val,
                                       Offset size)
{
    std::vector<DefSite> result;

    auto psn = PTA->getLLVMPointsToChecked(val);
    if (!psn.first) {
        result.push_back(DefSite(UNKNOWN_MEMORY));
#ifndef NDEBUG
        llvm::errs() << "[RD] warning at: " << ValInfo(where) << "\n";
        llvm::errs() << "No points-to set for: " << ValInfo(val) << "\n";
#endif
        // don't have points-to information for used pointer
        return result;
    }

    if (psn.second.empty()) {
#ifndef NDEBUG
        llvm::errs() << "[RD] warning at: " << ValInfo(where) << "\n";
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
        result.push_back(DefSite(UNKNOWN_MEMORY));
        return result;
    }

    result.reserve(psn.second.size());

    if (psn.second.hasUnknown()) {
        result.push_back(DefSite(UNKNOWN_MEMORY));
    }

    for (const auto& ptr: psn.second) {
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        RWNode *ptrNode = getOperand(ptr.value);
        if (!ptrNode) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RD] error at "  << ValInfo(where) << "\n";
                llvm::errs() << "[RD] error for " << ValInfo(val) << "\n";
                llvm::errs() << "[RD] error: Cannot find node for "
                             << ValInfo(ptr.value) << "\n";
            }
            continue;
        }

        // FIXME: we should pass just size to the DefSite ctor, but the old code relies
        // on the behavior that when offset is unknown, the length is also unknown.
        // So for now, mimic the old code. Remove it once we fix the old code.
        result.push_back(DefSite(ptrNode, ptr.offset,
                                 ptr.offset.isUnknown() ?
                                    Offset::UNKNOWN : size));
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
            llvm::errs() << "[RWG] error: cannot find an operand: "
                         << *val << "\n";
            abort();
        }
    }
    assert(op && "Do not have an operand");
    return op;
}

#if 0
std::pair<RWNode *, RWNode *> LLVMReadWriteGraphBuilder::buildGlobals()
{
    RWNode *cur = nullptr, *prev, *first = nullptr;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = create(RWNodeType::ALLOC);
        addNode(&*I, cur);

        // add the initial global definitions
        if (auto GV = llvm::dyn_cast<llvm::GlobalVariable>(&*I)) {
            auto size = llvmutils::getAllocatedSize(GV->getType()->getContainedType(0),
                                                    &M->getDataLayout());
            if (size == 0)
                size = Offset::UNKNOWN;

            cur->addDef(cur, 0, size, true /* strong update */);
        }

        if (prev)
            makeEdge(prev, cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return std::pair<RWNode *, RWNode *>(first, cur);
}
#endif

} // namespace dda
} // namespace dg

