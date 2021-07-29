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

#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerGraphOptimizations.h"
#include "dg/llvm/PointerAnalysis/PointerGraph.h"

#include "llvm/PointerAnalysis/PointerGraphValidator.h"
#include "llvm/llvm-utils.h"

#include "dg/util/debug.h"

namespace dg {
namespace pta {

void LLVMPointerGraphBuilder::insertPthreadCreateByPtrCall(PSNode *callsite) {
    auto *fork =
            createForkNode(callsite->getUserData<llvm::CallInst>(), callsite);
    fork->setCallInst(callsite);
    callsite->addSuccessor(fork);
}

void LLVMPointerGraphBuilder::insertPthreadJoinByPtrCall(PSNode *callsite) {
    auto *join =
            createJoinNode(callsite->getUserData<llvm::CallInst>(), callsite);
    join->setCallInst(callsite);
    join->insertAfter(callsite);
}

PSNodeJoin *
LLVMPointerGraphBuilder::findJoin(const llvm::CallInst *callInst) const {
    for (auto *join : joinNodes) {
        if (join->getUserData<llvm::CallInst>() == callInst)
            return join;
    }
    return nullptr;
}

bool LLVMPointerGraphBuilder::addFunctionToFork(PSNode *function,
                                                PSNodeFork *forkNode) {
    const llvm::CallInst *CInst =
            forkNode->callInst()->getUserData<llvm::CallInst>();
    const llvm::Function *F = function->getUserData<llvm::Function>();

    DBG(pta,
        "Function '" << F->getName().str() << "' can be spawned via thread");

    PointerSubgraph &subgraph = createOrGetSubgraph(F);
    addInterproceduralPthreadOperands(F, CInst);
    // FIXME: create fork and join edges
    forkNode->addSuccessor(subgraph.root);
    forkNode->addFunction(function);

    return true;
}

bool LLVMPointerGraphBuilder::addFunctionToJoin(PSNode *function,
                                                PSNodeJoin *joinNode) {
    PSNode *CInst = joinNode->getPairedNode();
    joinNode->addFunction(function);
    const llvm::Function *F = function->getUserData<llvm::Function>();

    if (!F->empty()) {
        PointerSubgraph *subgraph = getSubgraph(F);
        assert(subgraph && "Did not build the subgraph for thread");

        DBG(pta, "Found a new join point for function '" << F->getName().str()
                                                         << "'");
        if (!CInst->getOperand(1)->isNull()) {
            PSNode *phi = PS.create<PSNodeType::PHI>();
            PSNode *store =
                    PS.create<PSNodeType::STORE>(phi, CInst->getOperand(1));
            phi->addSuccessor(store);
            store->addSuccessor(joinNode);
            for (PSNode *ret : subgraph->returnNodes) {
                ret->addSuccessor(phi);
                phi->addOperand(ret);
            }
        } else {
            for (PSNode *ret : subgraph->returnNodes) {
                ret->addSuccessor(joinNode);
            }
        }
    }
    return true;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createPthreadCreate(const llvm::CallInst *CInst) {
    PSNodeCall *callNode = PSNodeCall::get(PS.create<PSNodeType::CALL>());
    PSNodeFork *forkNode = createForkNode(CInst, callNode);

    callNode->addSuccessor(forkNode);

    // do not add the fork node into the sequence,
    // it is going to branch from the call
    return {callNode};
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createPthreadJoin(const llvm::CallInst *CInst) {
    PSNodeCall *callNode = PSNodeCall::get(PS.create<PSNodeType::CALL>());
    PSNodeJoin *joinNode = createJoinNode(CInst, callNode);

    // FIXME: create only the join node, not call node and join node
    return {callNode, joinNode};
}

PSNodeFork *LLVMPointerGraphBuilder::createForkNode(const llvm::CallInst *CInst,
                                                    PSNode *callNode) {
    using namespace llvm;
    PSNodeFork *forkNode = PSNodeFork::get(
            PS.create<PSNodeType::FORK>(getOperand(CInst->getArgOperand(2))));
    callNode->setPairedNode(forkNode);
    forkNode->setPairedNode(callNode);

    forkNode->setCallInst(callNode);

    forkNodes.push_back(forkNode);
    addArgumentOperands(*CInst, *callNode);

    const Value *spawnedFunc = CInst->getArgOperand(2)->stripPointerCasts();
    if (const Function *func = dyn_cast<Function>(spawnedFunc)) {
        addFunctionToFork(getNodes(func)->getSingleNode(), forkNode);
    }

    return forkNode;
}

PSNodeJoin *LLVMPointerGraphBuilder::createJoinNode(const llvm::CallInst *CInst,
                                                    PSNode *callNode) {
    using namespace llvm;

    PSNodeJoin *joinNode = PSNodeJoin::get(PS.create<PSNodeType::JOIN>());
    callNode->setPairedNode(joinNode);
    joinNode->setPairedNode(callNode);

    joinNode->setCallInst(callNode);

    joinNodes.push_back(joinNode);
    addArgumentOperands(*CInst, *callNode);

    return joinNode;
}

LLVMPointerGraphBuilder::PSNodesSeq
LLVMPointerGraphBuilder::createPthreadExit(const llvm::CallInst *CInst) {
    using namespace llvm;

    PSNodeCall *callNode = PSNodeCall::get(PS.create<PSNodeType::CALL>());
    addArgumentOperands(*CInst, *callNode);
    auto *pthread_exit_operand = callNode->getOperand(0);

    PSNodeRet *returnNode =
            PSNodeRet::get(PS.create<PSNodeType::RETURN>(pthread_exit_operand));
    callNode->setPairedNode(returnNode);
    returnNode->setPairedNode(callNode);
    callNode->addSuccessor(returnNode);

    return {callNode, returnNode};
}

bool LLVMPointerGraphBuilder::matchJoinToRightCreate(PSNode *joinNode) {
    using namespace llvm;
    using namespace dg::pta;
    PSNodeJoin *join = PSNodeJoin::get(joinNode);
    PSNode *pthreadJoinCall = join->getPairedNode();

    PSNode *loadNode = pthreadJoinCall->getOperand(0);
    PSNode *joinThreadHandlePtr = loadNode->getOperand(0);
    bool changed = false;
    for (auto *fork : getForks()) {
        auto *pthreadCreateCall = fork->getPairedNode();
        auto *createThreadHandlePtr = pthreadCreateCall->getOperand(0);

        std::set<PSNode *> threadHandleIntersection;
        for (const auto &createPointsTo : createThreadHandlePtr->pointsTo) {
            for (const auto &joinPointsTo : joinThreadHandlePtr->pointsTo) {
                if (createPointsTo.target == joinPointsTo.target) {
                    threadHandleIntersection.insert(createPointsTo.target);
                }
            }
        }

        if (!threadHandleIntersection
                     .empty()) { // TODO refactor this into method for finding
                                 // new functions
            PSNode *func = pthreadCreateCall->getOperand(2);
            const llvm::Value *V = func->getUserData<llvm::Value>();
            auto pointsToFunctions = getPointsToFunctions(V);

            auto oldFunctions = join->functions();
            for (auto *function : pointsToFunctions) {
                if (join->functions().count(function) == 0) {
                    changed |= addFunctionToJoin(function, join);
                }
            }
            if (changed) {
                join->addFork(fork);
            }
        }
    }
    return changed;
}

} // namespace pta
} // namespace dg
