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

#include "dg/llvm/PointerAnalysis/PointerGraph.h"

#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

#if 0
RWNode *LLVMReadWriteGraphBuilder::createUndefinedCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    assert((nodes_map.find(CInst) == nodes_map.end())
           && "Adding node we already have");

    RWNode *node = create(RWNodeType::CALL);
    addNode(CInst, node);

    // if we assume that undefined functions are pure
    // (have no side effects), we can bail out here
    if (_options.undefinedArePure)
        return node;

    // every pointer we pass into the undefined call may be defined
    // in the function
    for (unsigned int i = 0; i < CInst->getNumArgOperands(); ++i) {
        const Value *llvmOp = CInst->getArgOperand(i);

        // constants cannot be redefined except for global variables
        // (that are constant, but may point to non constant memory
        const Value *strippedValue = llvmOp->stripPointerCasts();
        if (isa<Constant>(strippedValue)) {
            const GlobalVariable *GV = dyn_cast<GlobalVariable>(strippedValue);
            // if the constant is not global variable,
            // or the global variable points to constant memory
            if (!GV || GV->isConstant())
                continue;
        }

        auto pts = PTA->getLLVMPointsToChecked(llvmOp);
        // if we do not have a pts, this is not pointer
        // relevant instruction. We must do it this way
        // instead of type checking, due to the inttoptr.
        if (!pts.first)
            continue;

        for (const auto& ptr : pts.second) {
            if (llvm::isa<llvm::Function>(ptr.value))
                // function may not be redefined
                continue;

            RWNode *target = getOperand(ptr.value);
            assert(target && "Don't have pointer target for call argument");

            // this call may use and define this memory
            node->addDef(target, Offset::UNKNOWN, Offset::UNKNOWN);
            node->addUse(target, Offset::UNKNOWN, Offset::UNKNOWN);
        }
    }

    // XXX: to be completely correct, we should assume also modification
    // of all global variables, so we should perform a write to
    // unknown memory instead of the loop above

    return node;
}

bool LLVMReadWriteGraphBuilder::isInlineAsm(const llvm::Instruction *instruction)
{
    const llvm::CallInst *callInstruction = llvm::cast<llvm::CallInst>(instruction);
    return callInstruction->isInlineAsm();
}

void LLVMReadWriteGraphBuilder::matchForksAndJoins()
{
    using namespace llvm;
    using namespace pta;

    ForkJoinAnalysis FJA{PTA};

    for (auto& it : threadJoinCalls) {
        // it.first -> llvm::CallInst, it.second -> RWNode *
        auto functions = FJA.joinFunctions(it.first);
        for (auto llvmFunction : functions) {
            auto graphIterator = subgraphs_map.find(llvmFunction);
            for (auto returnNode : graphIterator->second.returns) {
                makeEdge(returnNode, it.second);
            }
        }
    }
}

RWNode *LLVMReadWriteGraphBuilder::createIntrinsicCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(CInst);
    const Value *dest;
    const Value *lenVal;

    RWNode *ret;
    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::memset:
            // memcpy/set <dest>, <src/val>, <len>
            dest = I->getOperand(0);
            lenVal = I->getOperand(2);
            break;
        case Intrinsic::vastart:
            // we create this node because this nodes works
            // as ALLOC in points-to, so we can have
            // reaching definitions to that
            ret = create(RWNodeType::CALL);
            ret->addDef(ret, 0, Offset::UNKNOWN);
            addNode(CInst, ret);
            return ret;
        default:
            return createUndefinedCall(CInst);
    }

    ret = create(RWNodeType::CALL);
    addNode(CInst, ret);

    auto pts = PTA->getLLVMPointsToChecked(dest);
    if (!pts.first) {
        llvm::errs() << "[RD] Error: No points-to information for destination in\n";
        llvm::errs() << ValInfo(I) << "\n";
        // continue, the points-to set is {unknown}
    }

    uint64_t len = Offset::UNKNOWN;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(lenVal))
        len = C->getLimitedValue();

    for (const auto& ptr : pts.second) {
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        Offset from, to;
        if (ptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, use whole memory
            from = Offset::UNKNOWN;
            len = Offset::UNKNOWN;
        } else {
            from = *ptr.offset;
        }

        // do not allow overflow
        if (Offset::UNKNOWN - *from > len)
            to = from + len;
        else
            to = Offset::UNKNOWN;

        RWNode *target = getOperand(ptr.value);
        //assert(target && "Don't have pointer target for intrinsic call");
        if (!target) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RD] error at " << ValInfo(CInst) << "\n"
                             << "[RD] error: Haven't created node for: "
                             << ValInfo(ptr.value) << "\n";
            }
            target = UNKNOWN_MEMORY;
        }

        // add the definition
        ret->addDef(target, from, to, !from.isUnknown() && !to.isUnknown() /* strong update */);
    }

    return ret;
}

template <typename T>
std::pair<Offset, Offset> getFromTo(const llvm::CallInst *CInst, T what) {
    auto from = what->from.isOperand()
                ? llvmutils::getConstantValue(CInst->getArgOperand(what->from.getOperand()))
                  : what->from.getOffset();
    auto to = what->to.isOperand()
                ? llvmutils::getConstantValue(CInst->getArgOperand(what->to.getOperand()))
                  : what->to.getOffset();

    return {from, to};
}

RWNode *LLVMReadWriteGraphBuilder::funcFromModel(const FunctionModel *model,
                                                 const llvm::CallInst *CInst) {

    RWNode *node = create(RWNodeType::CALL);

    for (unsigned int i = 0; i < CInst->getNumArgOperands(); ++i) {
        if (!model->handles(i))
            continue;

        const auto llvmOp = CInst->getArgOperand(i);
        auto pts = PTA->getLLVMPointsToChecked(llvmOp);
        // if we do not have a pts, this is not pointer
        // relevant instruction. We must do it this way
        // instead of type checking, due to the inttoptr.
        if (!pts.first) {
            llvm::errs() << "[Warning]: did not find pt-set for modeled function\n";
            llvm::errs() << "           Func: " << model->name << ", operand " << i << "\n";
            continue;
        }

        for (const auto& ptr : pts.second) {
            if (llvm::isa<llvm::Function>(ptr.value))
                // functions may not be redefined
                continue;

            RWNode *target = getOperand(ptr.value);
            assert(target && "Don't have pointer target for call argument");

            Offset from, to;
            if (auto defines = model->defines(i)) {
                std::tie(from, to) = getFromTo(CInst, defines);
                // this call may define this memory
                bool strong_updt = pts.second.size() == 1 &&
                                   !ptr.offset.isUnknown() &&
                                   !(ptr.offset + from).isUnknown() &&
                                   !(ptr.offset + to).isUnknown() &&
                                   !llvm::isa<llvm::CallInst>(ptr.value);
                // FIXME: what about vars in recursive functions?
                node->addDef(target, ptr.offset + from, ptr.offset + to, strong_updt);
            }
            if (auto uses = model->uses(i)) {
                std::tie(from, to) = getFromTo(CInst, uses);
                // this call uses this memory
                node->addUse(target, ptr.offset + from, ptr.offset + to);
            }
        }
    }

    return node;
}
RWNode *
LLVMReadWriteGraphBuilder::createCallToZeroSizeFunction(const llvm::Function *function,
                                            const llvm::CallInst *CInst)
{
    if (function->isIntrinsic()) {
        return createIntrinsicCall(CInst);
    }
    if (_options.threads) {
        if (function->getName() == "pthread_create") {
            return createPthreadCreateCalls(CInst);
        } else if (function->getName() == "pthread_join") {
            return createPthreadJoinCall(CInst);
        } else if (function->getName() == "pthread_exit") {
            return createPthreadExitCall(CInst);
        }
    }

    auto type = _options.getAllocationFunction(function->getName());
    if (type != AllocationFunction::NONE) {
        if (type == AllocationFunction::REALLOC)
            return createRealloc(CInst);
        else
            return createDynAlloc(CInst, type);
    } else {
        return createUndefinedCall(CInst);
    }

    assert(false && "Unreachable");
    abort();
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadCreateCalls(const llvm::CallInst *CInst)
{
    using namespace llvm;

    RWNode *rootNode = create(RWNodeType::FORK);
    auto iterator = nodes_map.find(CInst);
    if (iterator == nodes_map.end()) {
        addNode(CInst, rootNode);
    } else {
        assert(iterator->second->getType() == RWNodeType::CALL && "Adding node we already have");
        addArtificialNode(CInst, rootNode);
    }
    threadCreateCalls.emplace(CInst, rootNode);

    Value *calledValue = CInst->getArgOperand(2);
    const auto& functions = getCalledFunctions(calledValue, PTA);

    for (const Function *function : functions) {
        if (function->isDeclaration()) {
            llvm::errs() << "[RD] error: phtread_create spawns undefined function: "
                         << function->getName() << "\n";
            continue;
        }
        auto subg = getOrCreateSubgraph(function);
        makeEdge(rootNode, subg->entry->nodes.front());
    }
    return rootNode;
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadJoinCall(const llvm::CallInst *CInst)
{
    // TODO later change this to create join node and set data correctly
    // we need just to create one node;
    // undefined call is overapproximation, so its ok
    RWNode *node = createUndefinedCall(CInst);
    threadJoinCalls.emplace(CInst, node);
    return node;
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadExitCall(const llvm::CallInst *CInst)
{
    return createReturn(CInst);
}
#endif  // 0

} // namespace dda
} // namespace dg
