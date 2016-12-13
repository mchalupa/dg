#include <cassert>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Config/llvm-config.h>

#if (LLVM_VERSION_MINOR < 5)
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

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "analysis/PointsTo/PointerSubgraph.h"
#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

PSNode *
LLVMPointerSubgraphBuilder::handleGlobalVariableInitializer(const llvm::Constant *C,
                                                PSNode *node)
{
    using namespace llvm;
    PSNode *last = node;

    // if the global is zero initialized, just set the zeroInitialized flag
    if (C->isNullValue()) {
        node->setZeroInitialized();
    } else if (C->getType()->isAggregateType()) {
        uint64_t off = 0;
        for (auto I = C->op_begin(), E = C->op_end(); I != E; ++I) {
            const Value *val = *I;
            Type *Ty = val->getType();

            if (Ty->isPointerTy()) {
                PSNode *op = getOperand(val);
                PSNode *target = new PSNode(CONSTANT, node, off);
                // FIXME: we're leaking the target
                // NOTE: mabe we could do something like
                // CONSTANT_STORE that would take Pointer instead of node??
                // PSNode(CONSTANT_STORE, op, Pointer(node, off)) or
                // PSNode(COPY, op, Pointer(node, off))??
                PSNode *store = new PSNode(STORE, op, target);
                store->insertAfter(last);
                last = store;
            }

            off += DL->getTypeAllocSize(Ty);
        }
    } else if (isa<ConstantExpr>(C) || isa<Function>(C)
                || C->getType()->isPointerTy()) {
       if (C->getType()->isPointerTy()) {
           PSNode *value = getOperand(C);
           assert(value->pointsTo.size() == 1 && "BUG: We should have constant");
           // FIXME: we're leaking the target
           PSNode *store = new PSNode(STORE, value, node);
           store->insertAfter(last);
           last = store;
       }
    } else if (!isa<ConstantInt>(C) && !isa<ConstantFP>(C)) {
        llvm::errs() << *C << "\n";
        llvm::errs() << "ERROR: ^^^ global variable initializer not handled\n";
        abort();
    }

    return last;
}

PSNodesSeq LLVMPointerSubgraphBuilder::buildGlobals()
{
    PSNode *cur = nullptr, *prev, *first = nullptr;
    // create PointerSubgraph nodes
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = new PSNode(pta::ALLOC);
        addNode(&*I, cur);

        if (prev)
            prev->addSuccessor(cur);
        else
            first = cur;
    }

    // only now handle the initializers - we need to have then
    // built, because they can point to each other
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        PSNode *node = getNode(&*I);
        assert(node && "BUG: Do not have global variable");

        // handle globals initialization
        const llvm::GlobalVariable *GV
                            = llvm::dyn_cast<llvm::GlobalVariable>(&*I);
        if (GV && GV->hasInitializer() && !GV->isExternallyInitialized()) {
            const llvm::Constant *C = GV->getInitializer();
            cur = handleGlobalVariableInitializer(C, node);
        } else {
            // without initializer we can not do anything else than
            // assume that it can point everywhere
            cur = new PSNode(pta::STORE, UNKNOWN_MEMORY, node);
            cur->insertAfter(node);
        }
    }

    assert((!first && !cur) || (first && cur));
    return std::make_pair(first, cur);
}

} // namespace pta
} // namespace analysis
} // namespace dg
