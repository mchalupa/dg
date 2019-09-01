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

#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerGraphOptimizations.h"
#include "dg/llvm/analysis/PointsTo/PointerGraph.h"

#include "llvm/analysis/PointsTo/PointerGraphValidator.h"
#include "llvm/llvm-utils.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {
namespace pta {

void LLVMPointerGraphBuilder::insertPthreadCreateByPtrCall(PSNode *callsite) {
    auto seq = createFork(callsite->getUserData<llvm::CallInst>());
    seq.getLast()->addSuccessor(callsite->getSingleSuccessor());
    callsite->replaceSingleSuccessor(seq.getFirst());
    PSNodeFork::cast(seq.getLast())->setCallInst(callsite);
}

void LLVMPointerGraphBuilder::insertPthreadJoinByPtrCall(PSNode *callsite) {
    auto seq = createJoin(callsite->getUserData<llvm::CallInst>());
    seq.getLast()->addSuccessor(callsite->getSingleSuccessor());
    callsite->replaceSingleSuccessor(seq.getFirst());
    PSNodeJoin::cast(seq.getLast())->setCallInst(callsite);
}

std::map<const llvm::CallInst *, PSNodeJoin *>
LLVMPointerGraphBuilder::getJoins() const {
    return threadJoinCalls;
}

std::map<const llvm::CallInst *, PSNodeFork *>
LLVMPointerGraphBuilder::getForks() const {
    return threadCreateCalls;
}

PSNodeJoin *
LLVMPointerGraphBuilder::findJoin(const llvm::CallInst *callInst) const {
    auto iterator = threadJoinCalls.find(callInst);
    if (iterator != threadJoinCalls.end()) {
        return iterator->second;
    }
    return nullptr;
}

bool LLVMPointerGraphBuilder::addFunctionToFork(PSNode *function, 
                                                PSNodeFork * forkNode) {
    const llvm::CallInst *CInst = forkNode->callInst()->getUserData<llvm::CallInst>();
    bool changed = false; 
    auto functions = forkNode->functions();
    if (std::find(functions.cbegin(), 
                  functions.cend(), 
                  function) == functions.cend()) {
        changed = true;
        const llvm::Function *F = function->getUserData<llvm::Function>(); 
        PointerSubgraph& subgraph = createOrGetSubgraph(F);
        addInterproceduralPthreadOperands(F, CInst);
        forkNode->addSuccessor(subgraph.root);
        forkNode->addFunction(function);
    }
    return changed;
}

bool LLVMPointerGraphBuilder::addFunctionToJoin(PSNode *function, 
                                                PSNodeJoin * joinNode) {
    PSNode * CInst = joinNode->getPairedNode();
    joinNode->addFunction(function);
    const llvm::Function *F = function->getUserData<llvm::Function>();
    if (F->size() != 0) {
        PointerSubgraph& subgraph = createOrGetSubgraph(F);
        if (!CInst->getOperand(1)->isNull()) {
            PSNode *phi = PS.create(PSNodeType::PHI, nullptr);
            PSNode *store = PS.create(PSNodeType::STORE, 
                                      phi, 
                                      CInst->getOperand(1));
            phi->addSuccessor(store);
            store->addSuccessor(joinNode);
            for (PSNode *ret : subgraph.returnNodes) {
                phi->addOperand(ret);
            }
        }
    }
    return true;
}

LLVMPointerGraphBuilder::PSNodesSeq&
LLVMPointerGraphBuilder::createFork(const llvm::CallInst *CInst) {

    using namespace llvm;
    PSNodeCall *callNode = PSNodeCall::get(PS.create(PSNodeType::CALL));
    PSNodeFork *forkNode = PSNodeFork::get(PS.create(PSNodeType::FORK));
    callNode->setPairedNode(forkNode);
    forkNode->setPairedNode(callNode);

    if (auto nds = getNodes(CInst)) {
        // CInst is already in nodes_map - probably function pointer call
        forkNode->setCallInst(nds->getFirst());
    } else {
        forkNode->setCallInst(callNode);
    }

    threadCreateCalls.emplace(CInst, forkNode);
    addArgumentOperands(*CInst, *callNode);

    const Value *functionToBeCalledOperand = CInst->getArgOperand(2);
    if (const Function *func = dyn_cast<Function>(functionToBeCalledOperand)) {
        addFunctionToFork(nodes_map[func].getSingleNode(), forkNode);
    }

    return addNode(CInst, {callNode, forkNode});
}

LLVMPointerGraphBuilder::PSNodesSeq&
LLVMPointerGraphBuilder::createJoin(const llvm::CallInst *CInst) {
    using namespace llvm;

    PSNodeCall *callNode = PSNodeCall::get(PS.create(PSNodeType::CALL));
    PSNodeJoin *joinNode = PSNodeJoin::get(PS.create(PSNodeType::JOIN));
    callNode->setPairedNode(joinNode);
    joinNode->setPairedNode(callNode);
    callNode->addSuccessor(joinNode);

    if (auto nds = getNodes(CInst)) {
        // CInst is already in nodes_map - probably function pointer call
        joinNode->setCallInst(nds->getFirst());
    } else {
        joinNode->setCallInst(callNode);
    }

    threadJoinCalls.emplace(CInst, joinNode);
    addArgumentOperands(*CInst, *callNode);

    return addNode(CInst, {callNode, joinNode});
}

LLVMPointerGraphBuilder::PSNodesSeq&
LLVMPointerGraphBuilder::createPthreadExit(const llvm::CallInst *CInst) {
    using namespace llvm;

    PSNodeCall *callNode = PSNodeCall::get(PS.create(PSNodeType::CALL));
    addArgumentOperands(*CInst, *callNode);
    auto pthread_exit_operand = callNode->getOperand(0);
    PSNodeRet *returnNode
        = PSNodeRet::get(PS.create(PSNodeType::RETURN,
                                   pthread_exit_operand, nullptr));
    callNode->setPairedNode(returnNode);
    returnNode->setPairedNode(callNode);
    callNode->addSuccessor(returnNode);

    return addNode(CInst, {callNode, returnNode});
}

bool LLVMPointerGraphBuilder::matchJoinToRightCreate(PSNode *joinNode) {
    using namespace llvm;
    using namespace dg::analysis::pta;
    PSNodeJoin *join = PSNodeJoin::get(joinNode);
    PSNode *pthreadJoinCall = join->getPairedNode();
    
    PSNode *loadNode = pthreadJoinCall->getOperand(0);
    PSNode *joinThreadHandlePtr = loadNode->getOperand(0);
    bool changed = false;
    for (auto & instNodeAndForkNode : threadCreateCalls) {
        auto pthreadCreateCall = instNodeAndForkNode.second->getPairedNode();
        auto createThreadHandlePtr = pthreadCreateCall->getOperand(0);
 
        std::set<PSNode *> threadHandleIntersection;
        for (const auto createPointsTo : createThreadHandlePtr->pointsTo) {
            for (const auto joinPointsTo : joinThreadHandlePtr->pointsTo) {
                if (createPointsTo.target == joinPointsTo.target) {
                    threadHandleIntersection.insert(createPointsTo.target);
                }
            }
        }

        
        if (!threadHandleIntersection.empty()) {//TODO refactor this into method for finding new functions
            PSNode *func = pthreadCreateCall->getOperand(2);
            const llvm::Value *V = func->getUserData<llvm::Value>();
            auto pointsToFunctions = getPointsToFunctions(V); 
            auto oldFunctions = join->functions();
            std::set<PSNode *> newFunctions;
            std::sort(pointsToFunctions.begin(), pointsToFunctions.end());
            std::set_difference(pointsToFunctions.begin(), pointsToFunctions.end(), 
                                  oldFunctions.begin(),      oldFunctions.end(), 
                                  std::inserter(newFunctions, newFunctions.begin()));
            for (const auto &function : newFunctions) {
                changed |= addFunctionToJoin(function, 
                                             join); 
            }
            if (changed) {
                join->addFork(instNodeAndForkNode.second);
            }
        }
    }
    return changed;
}

} // namespace pta
} // namespace analysis
} // namespace dg
