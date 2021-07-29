#include <cassert>
#include <vector>

#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

//#include "dg/llvm/PointerAnalysis/PointerGraph.h"
//#include "llvm/ForkJoin/ForkJoin.h"
#include "dg/ReadWriteGraph/RWNode.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

static inline llvm::Value *getMemIntrinsicValueOp(llvm::MemIntrinsic *MI) {
    switch (MI->getIntrinsicID()) {
    case llvm::Intrinsic::memmove:
    case llvm::Intrinsic::memcpy:
    case llvm::Intrinsic::memset:
        return MI->getOperand(1);
        break;
    default:
        assert(false && "Unsupported intrinsic");
    }
    return nullptr;
}

RWNode *LLVMReadWriteGraphBuilder::createAlloc(const llvm::Instruction *Inst) {
    using namespace llvm;

    RWNode &node = create(RWNodeType::ALLOC);

    // check if the address of this alloca is taken
    for (auto I = Inst->use_begin(), E = Inst->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        auto *use = *I;
#else
        auto *use = I->getUser();
#endif
        if (auto *S = dyn_cast<StoreInst>(use)) {
            if (S->getValueOperand() == Inst) {
                node.setAddressTaken();
                break;
            }
        } else if (auto *MI = dyn_cast<MemIntrinsic>(use)) {
            if (getMemIntrinsicValueOp(MI) == Inst) {
                node.setAddressTaken();
                break;
            }
        } else if (const auto *I = dyn_cast<Instruction>(Inst)) {
            assert(!I->mayWriteToMemory() &&
                   "Unhandled memory-writing instruction");
            (void) I; // c++17 TODO: replace with [[maybe_unused]]
        }
    }

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(Inst)) {
        auto size = llvmutils::getAllocatedSize(AI, getDataLayout());
        node.setSize(size);
        // this is a alloca that does not have the address taken,
        // therefore we must always access the last instance in loads
        // (even in recursive functions) and may terminate the search
        // for definitions of this alloca at this alloca
        if (!node.hasAddressTaken()) {
            node.addOverwrites(&node, 0, size > 0 ? size : Offset::UNKNOWN);
        }
    }
    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createDynAlloc(const llvm::Instruction *Inst,
                                                  AllocationFunction type) {
    using namespace llvm;

    RWNode &node = create(RWNodeType::DYN_ALLOC);
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

RWNode *
LLVMReadWriteGraphBuilder::createRealloc(const llvm::Instruction *Inst) {
    RWNode &node = create(RWNodeType::DYN_ALLOC);

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
        // NOTE: do not use mapPointers, it could lead to infinite
        // recursion as realloc may use itself and the 'node' is not
        // in the map yet
        addReallocUses(Inst, node, size);
    }
    return &node;
}

void LLVMReadWriteGraphBuilder::addReallocUses(const llvm::Instruction *Inst,
                                               RWNode &node, uint64_t size) {
    auto psn = PTA->getLLVMPointsToChecked(Inst->getOperand(0));
    if (!psn.first) {
#ifndef NDEBUG
        llvm::errs() << "[RWG] warning at: " << ValInfo(Inst) << "\n";
        llvm::errs() << "No points-to set for: " << ValInfo(Inst->getOperand(0))
                     << "\n";
#endif
        node.addUse(UNKNOWN_MEMORY);
        return;
    }

    if (psn.second.empty()) {
#ifndef NDEBUG
        llvm::errs() << "[RWG] warning at: " << ValInfo(Inst) << "\n";
        llvm::errs() << "Empty points-to set for: "
                     << ValInfo(Inst->getOperand(0)) << "\n";
#endif
        node.addUse(UNKNOWN_MEMORY);
        return;
    }

    if (psn.second.hasUnknown()) {
        node.addUse(UNKNOWN_MEMORY);
    }

    for (const auto &ptr : psn.second) {
        // realloc may be only from other dynamic allocation
        if (!llvm::isa<llvm::CallInst>(ptr.value))
            continue;

        // llvm::errs() << "Realloc ptr: " << *ptr.value << "\n";
        RWNode *ptrNode = nullptr;
        if (ptr.value == Inst) {
            // the realloc reallocates itself
            ptrNode = &node;
        } else {
            ptrNode = getOperand(ptr.value);
        }
        if (!ptrNode) {
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RWG] error at " << ValInfo(Inst) << "\n";
                llvm::errs() << "[RWG] error for "
                             << ValInfo(Inst->getOperand(0)) << "\n";
                llvm::errs() << "[RWG] error: Cannot find node for "
                             << ValInfo(ptr.value) << "\n";
            }
            continue;
        }

        node.addUse(ptrNode, ptr.offset,
                    ptr.offset.isUnknown() ? Offset::UNKNOWN : size);
    }
}

RWNode *
LLVMReadWriteGraphBuilder::createReturn(const llvm::Instruction * /*unused*/) {
    return &create(RWNodeType::RETURN);
}

RWNode *LLVMReadWriteGraphBuilder::createStore(const llvm::Instruction *Inst) {
    RWNode &node = create(RWNodeType::STORE);

    uint64_t size = llvmutils::getAllocatedSize(Inst->getOperand(0)->getType(),
                                                getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(1), size);

    // strong update is possible only with must aliases that point
    // to the last instance of the memory object. Since detecting that
    // is not that easy, do strong updates only on must aliases
    // of local and global variables (and, of course, we must know the offsets)
    // FIXME: ALLOCAs in recursive procedures can also yield only weak update
    bool strong_update = false;
    if (defSites.size() == 1) {
        const auto &ds = *(defSites.begin());
        strong_update = (ds.target->isAlloc() || ds.target->isGlobal()) &&
                        !ds.offset.isUnknown() && !ds.len.isUnknown();
    }

    for (const auto &ds : defSites) {
        node.addDef(ds, strong_update);
    }

    return &node;
}

RWNode *LLVMReadWriteGraphBuilder::createLoad(const llvm::Instruction *Inst) {
    RWNode &node = create(RWNodeType::LOAD);

    uint64_t size =
            llvmutils::getAllocatedSize(Inst->getType(), getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(0), size);
    for (const auto &ds : defSites) {
        node.addUse(ds);
    }

    return &node;
}

RWNode *
LLVMReadWriteGraphBuilder::createAtomicRMW(const llvm::Instruction *Inst) {
    const auto *RMW = llvm::cast<llvm::AtomicRMWInst>(Inst);
    RWNode &node = create(RWNodeType::STORE);

    uint64_t size = llvmutils::getAllocatedSize(RMW->getValOperand()->getType(),
                                                getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(RMW, RMW->getPointerOperand(), size);

    // strong update is possible only with must aliases that point
    // to the last instance of the memory object. Since detecting that
    // is not that easy, do strong updates only on must aliases
    // of local and global variables (and, of course, we must know the offsets)
    // FIXME: ALLOCAs in recursive procedures can also yield only weak update
    bool strong_update = false;
    if (defSites.size() == 1) {
        const auto &ds = *(defSites.begin());
        strong_update = (ds.target->isAlloc() || ds.target->isGlobal()) &&
                        !ds.offset.isUnknown() && !ds.len.isUnknown();
    }

    for (const auto &ds : defSites) {
        node.addDef(ds, strong_update);
    }

    // RMW is also use
    for (const auto &ds : defSites) {
        node.addUse(ds);
    }

    return &node;
}

NodesSeq<RWNode>
LLVMReadWriteGraphBuilder::createCall(const llvm::Instruction *Inst) {
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
#if LLVM_VERSION_MAJOR >= 8
    const Value *calledVal = CInst->getCalledOperand()->stripPointerCasts();
#else
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
#endif
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

    const auto &functions = getCalledFunctions(calledVal, PTA);
    if (functions.empty()) {
        llvm::errs() << "[RWG] error: could not determine the called function "
                        "in a call via pointer: \n"
                     << ValInfo(CInst) << "\n";
        return {createUnknownCall(CInst)};
    }
    return createCallToFunctions(functions, CInst);
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst, OptsT &opts) {
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
#if LLVM_VERSION_MAJOR >= 8
    const Value *calledVal = CInst->getCalledOperand()->stripPointerCasts();
#else
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
#endif
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that
        return true;

    if (func->empty()) {
        // we have a model for this function
        if (opts.getFunctionModel(func->getName().str()))
            return true;
        // memory allocation
        if (opts.isAllocationFunction(func->getName().str()))
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
    } // we want defined function, since those can contain
    // pointer's manipulation and modify CFG
    return true;

    assert(0 && "We should not reach this");
}

NodesSeq<RWNode> LLVMReadWriteGraphBuilder::createNode(const llvm::Value *v) {
    using namespace llvm;
    if (isa<GlobalVariable>(v)) {
        // global variables are like allocations
        return {&create(RWNodeType::GLOBAL)};
    }

    const auto *I = dyn_cast<Instruction>(v);
    if (!I)
        return {};

    // we may created this node when searching for an operand
    switch (I->getOpcode()) {
    case Instruction::Alloca:
        // we need alloca's as target to DefSites
        return {createAlloc(I)};
    case Instruction::Store:
        return {createStore(I)};
    case Instruction::AtomicRMW:
        return {createAtomicRMW(I)};
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
