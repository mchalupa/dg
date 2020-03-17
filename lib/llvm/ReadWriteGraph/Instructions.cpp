#include <vector>
#include <cassert>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
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

//#include "dg/llvm/PointerAnalysis/PointerGraph.h"
//#include "llvm/ForkJoin/ForkJoin.h"
#include "dg/ReadWriteGraph/RWNode.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

RWNode *LLVMReadWriteGraphBuilder::createAlloc(const llvm::Instruction *Inst) {
    RWNode& node = create(RWNodeType::ALLOC);
    if (const llvm::AllocaInst *AI
            = llvm::dyn_cast<llvm::AllocaInst>(Inst))
        node.setSize(llvmutils::getAllocatedSize(AI, getDataLayout()));

    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createDynAlloc(const llvm::Instruction *Inst,
                                                  AllocationFunction type) {
    using namespace llvm;

    RWNode& node = create(RWNodeType::DYN_ALLOC);
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *op;
    uint64_t size = 0, size2 = 0;

    switch (type) {
        case AllocationFunction::MALLOC:
        case AllocationFunction::ALLOCA:
            op = CInst->getOperand(0);
            break;
        case AllocationFunction::CALLOC:
            op = CInst->getOperand(1);
            break;
        default:
            errs() << *CInst << "\n";
            assert(0 && "unknown memory allocation type");
            // for NDEBUG
            abort();
    };

    // infer allocated size
    size = llvmutils::getConstantValue(op);
    if (size != 0 && type == AllocationFunction::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = llvmutils::getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node.setSize(size);
    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createRealloc(const llvm::Instruction *Inst) {
    RWNode& node = create(RWNodeType::DYN_ALLOC);

    uint64_t size = llvmutils::getConstantValue(Inst->getOperand(1));
    if (size == 0)
        size = Offset::UNKNOWN;
    else
        node.setSize(size);

    // realloc defines itself, since it copies the values
    // from previous memory
    node.addDef(&node, 0, size, false /* strong update */);

    if (buildUses) {
        // realloc copies the memory
        auto defSites = mapPointers(Inst, Inst->getOperand(0), size);
        for (const auto& ds : defSites) {
            node.addUse(ds);
        }
    }

    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createReturn(const llvm::Instruction *) {
    return &create(RWNodeType::RETURN);
}

RWNode *LLVMReadWriteGraphBuilder::createStore(const llvm::Instruction *Inst) {
    RWNode& node = create(RWNodeType::STORE);

    uint64_t size = llvmutils::getAllocatedSize(Inst->getOperand(0)->getType(),
                                                getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(1), size);

    // strong update is possible only with must aliases. Also we can not
    // be pointing to heap, because then we don't know which object it
    // is in run-time, like:
    //  void *foo(int a)
    //  {
    //      void *mem = malloc(...)
    //      mem->n = a;
    //  }
    //
    //  1. mem1 = foo(3);
    //  2. mem2 = foo(4);
    //  3. assert(mem1->n == 3);
    //
    //  If we would do strong update on line 2 (which we would, since
    //  there we have must alias for the malloc), we would loose the
    //  definitions for line 1 and we would get incorrect results
    bool strong_update = false;
    if (defSites.size() == 1) {
        const auto& ds = *(defSites.begin());
        strong_update = !ds.offset.isUnknown() && !ds.len.isUnknown() &&
                         ds.target->getType() != RWNodeType::DYN_ALLOC;
    }

    for (const auto& ds : defSites) {
        node.addDef(ds, strong_update);
    }

    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createLoad(const llvm::Instruction *Inst) {
    RWNode& node = create(RWNodeType::LOAD);

    uint64_t size = llvmutils::getAllocatedSize(Inst->getType(),
                                                getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(0), size);
    for (const auto& ds : defSites) {
        node.addUse(ds);
    }

    return &node;
}

NodesSeq<RWNode>
LLVMReadWriteGraphBuilder::createCall(const llvm::Instruction *Inst) {
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    static bool warned_inline_assembly = false;

    if (CInst->isInlineAsm()) {
        if (!warned_inline_assembly) {
            llvm::errs() << "[RWG] WARNING: Inline assembler found\n";
            warned_inline_assembly = true;
        }
        return {createUnknownCall(CInst)};
    }

    if (const Function *function = dyn_cast<Function>(calledVal)) {
        return createCallToFunctions({function}, CInst);
    }


    const auto& functions = getCalledFunctions(calledVal, PTA);
    if (functions.empty()) {
        llvm::errs() << "[RD] error: could not determine the called function "
                        "in a call via pointer: \n"
                     << ValInfo(CInst) << "\n";
        return {createUnknownCall(CInst)};
    }
    return createCallToFunctions(functions, CInst);
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst,
                           OptsT& opts)
{
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that
        return true;

    if (func->size() == 0) {
        // we have a model for this function
        if (opts.getFunctionModel(func->getName()))
            return true;
        // memory allocation
        if (opts.isAllocationFunction(func->getName()))
            return true;

        if (func->isIntrinsic()) {
            switch (func->getIntrinsicID()) {
                case Intrinsic::memmove:
                case Intrinsic::memcpy:
                case Intrinsic::memset:
                case Intrinsic::vastart:
                    return true;
                default:
                    return false;
            }
        }

        // undefined function
        return true;
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

NodesSeq<RWNode> LLVMReadWriteGraphBuilder::createNode(const llvm::Value *v) {
    using namespace llvm;
    auto *I = dyn_cast<Instruction>(v);
    if (!I)
        return {};

    // we may created this node when searching for an operand
    switch(I->getOpcode()) {
         case Instruction::Alloca:
             // we need alloca's as target to DefSites
             return {createAlloc(I)};
         case Instruction::Store:
             return {createStore(I)};
         case Instruction::Load:
             if (buildUses) {
                 return {createLoad(I)};
             }
             break;
         case Instruction::Ret:
             // we need create returns, since
             // these modify CFG and thus data-flow
             return {createReturn(I)};
         case Instruction::Call:
             if (!isRelevantCall(I, _options))
                 break;

             return createCall(I);
    }

    return {};
}

} // namespace dda
} // namespace dg

