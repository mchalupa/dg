#include "GraphBuilder.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

#include "Nodes.h"
#include "BlockGraph.h"
#include "FunctionGraph.h"

#include <llvm/IR/CFG.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

#include <vector>

using namespace llvm;

GraphBuilder::GraphBuilder(dg::LLVMPointerAnalysis *pointsToAnalysis):pointsToAnalysis_(pointsToAnalysis)
{}

GraphBuilder::~GraphBuilder() {
    for (auto node : artificialNodes_) {
        delete node;
    }

    for (auto llvmAndNode : llvmToNodeMap_) {
        delete llvmAndNode.second;
    }

    for (auto iterator : llvmToBlockMap_) {
        delete iterator.second;
    }

    for (auto iterator : llvmToFunctionMap_) {
        delete iterator.second;
    }
}

template <>
ForkNode * GraphBuilder::addNode(ForkNode * node) {
    llvmToForks_.emplace(node->callInstruction(), node);
    if (node->isArtificial()) {
        if (this->artificialNodes_.insert(node).second) {
            return node;
        }
    } else {
        if (this->llvmToNodeMap_.emplace(node->llvmInstruction(), node).second) {
            return node;
        }
    }

    return nullptr;
}

template <>
JoinNode * GraphBuilder::addNode(JoinNode * node) {
    llvmToJoins_.emplace(node->callInstruction(), node);
    if (node->isArtificial()) {
        if (this->artificialNodes_.insert(node).second) {
            return node;
        }
    } else {
        if (this->llvmToNodeMap_.emplace(node->llvmInstruction(), node).second) {
            return node;
        }
    }

    return nullptr;
}

template <>
LockNode * GraphBuilder::addNode(LockNode * node) {
    llvmToLocks_.emplace(node->callInstruction(), node);
    if (node->isArtificial()) {
        if (this->artificialNodes_.insert(node).second) {
            return node;
        }
    } else {
        if (this->llvmToNodeMap_.emplace(node->llvmInstruction(), node).second) {
            return node;
        }
    }

    return nullptr;
}

template <>
UnlockNode * GraphBuilder::addNode(UnlockNode * node) {
    llvmToUnlocks_.emplace(node->callInstruction(), node);
    if (node->isArtificial()) {
        if (this->artificialNodes_.insert(node).second) {
            return node;
        }
    } else {
        if (this->llvmToNodeMap_.emplace(node->llvmInstruction(), node).second) {
            return node;
        }
    }

    return nullptr;
}

GraphBuilder::NodeSequence GraphBuilder::buildInstruction(const llvm::Instruction *instruction) {
    if (!instruction) {
        return {nullptr, nullptr};
    }

    auto inst = findInstruction(instruction);
    if (inst) {
        return {nullptr, nullptr};
    }
    NodeSequence sequence;
    switch (instruction->getOpcode()) {
    case Instruction::Call:
        sequence = buildCallInstruction(instruction);
        break;
    case Instruction::Ret:
        sequence = buildReturnInstruction(instruction);
        break;
    default:
        sequence = buildGeneralInstruction(instruction);
        break;
    }
    return sequence;
}

GraphBuilder::NodeSequence GraphBuilder::buildBlock(const llvm::BasicBlock *basicBlock) {
    if (!basicBlock) {
        return {nullptr, nullptr};
    }

    auto block = findBlock(basicBlock);
    if (block) {
        return {nullptr, nullptr};
    }
    std::vector<NodeSequence> builtInstructions;
    for (auto & instruction : *basicBlock) {
        auto builtInstruction = buildInstruction(&instruction);
        builtInstructions.push_back(builtInstruction);
        if (builtInstruction.second->getType() == NodeType::RETURN)
            break;
    }

    if (builtInstructions.size() > 1) {
        for (auto iterator = builtInstructions.begin() + 1;
                  iterator != builtInstructions.end();
                ++iterator) {
            (iterator - 1)->second->addSuccessor(iterator->first);
        }
    }

    Node * firstNode = builtInstructions.front().first;
    Node * lastNode = builtInstructions.back().second;

    auto blockGraph = new BlockGraph(basicBlock, firstNode, lastNode);
    this->llvmToBlockMap_.emplace(basicBlock, blockGraph);

    return  {firstNode, lastNode};
}

GraphBuilder::NodeSequence GraphBuilder::buildFunction(const llvm::Function *function) {
    if (!function) {
        return {nullptr, nullptr};
    }

    if (function->size() == 0) {
        return {nullptr, nullptr};
    }

    auto functionGraph = findFunction(function);
    if (functionGraph) {
        return {nullptr, nullptr};
    }

    //TODO refactor this into createFunctionGraph method
    auto entryNode = addNode(createNode<NodeType::ENTRY>());
    auto exitNode = addNode(createNode<NodeType::EXIT>());
    functionGraph = new FunctionGraph(function, entryNode, exitNode);
    this->llvmToFunctionMap_.emplace(function, functionGraph);

    for (auto & block : *function) {
        if (isReachable(&block)) {
            buildBlock(&block);
        }
    }

    for (auto & block : *function) {
        if (isReachable(&block)) {
            auto blockGraph = findBlock(&block);
            if (predecessorsNumber(&block) == 0) {
                functionGraph->entryNode()->addSuccessor(blockGraph->firstNode());
            }
            if (successorsNumber(&block) == 0) {
                blockGraph->lastNode()->addSuccessor(functionGraph->exitNode());
            }
            for (auto it = succ_begin(&block); it != succ_end(&block); ++it) {
                auto successorGraph = findBlock(*it);
                blockGraph->lastNode()->addSuccessor(successorGraph->firstNode());
            }
        }
    }

    return {functionGraph->entryNode(), functionGraph->exitNode()};
}

Node *GraphBuilder::findInstruction(const llvm::Instruction *instruction) {
    auto iterator = this->llvmToNodeMap_.find(instruction);
    if (iterator == this->llvmToNodeMap_.end()) {
        return nullptr;
    }
    return iterator->second;
}

BlockGraph *GraphBuilder::findBlock(const llvm::BasicBlock *basicBlock){
    if (!basicBlock) {
        return nullptr;
    }
    auto iterator = this->llvmToBlockMap_.find(basicBlock);
    if (iterator == this->llvmToBlockMap_.end()) {
        return nullptr;
    }
    return iterator->second;
}

FunctionGraph *GraphBuilder::findFunction(const llvm::Function *function) {
    auto iterator = this->llvmToFunctionMap_.find(function);
    if (iterator == this->llvmToFunctionMap_.end()) {
        return nullptr;
    }
    return iterator->second;
}

std::set<const CallInst *> GraphBuilder::getJoins() const {
    std::set<const CallInst *> llvmJoins;
    for (auto iterator : llvmToJoins_) {
        llvmJoins.insert(iterator.first);
    }
    return llvmJoins;
}

std::set<const CallInst *> GraphBuilder::getCorrespondingForks(const CallInst * callInst) const {
    std::set<const CallInst *> llvmForks;
    auto iterator = llvmToJoins_.find(callInst);
    if (iterator != llvmToJoins_.end()) {
        auto forks = iterator->second->correspondingForks();
        for (auto fork : forks) {
            llvmForks.insert(fork->callInstruction());
        }
    }
    return llvmForks;
}

std::set<LockNode *> GraphBuilder::getLocks() const {
    std::set<LockNode *> locks;
    for (auto iterator : llvmToLocks_) {
        locks.insert(iterator.second);
    }
    return locks;
}

bool GraphBuilder::matchForksAndJoins() {
    using namespace llvm;
    auto joinsMap = pointsToAnalysis_->getJoins();
    bool changed = false;
    for (auto &joinInstAndJoinNode : joinsMap) {
        auto PSJoinNode = joinInstAndJoinNode.second;
        auto callInst = getCallInst(PSJoinNode);
        auto iterator = llvmToJoins_.find(callInst);
        if (iterator != llvmToJoins_.end()) {
            auto joinNode = iterator->second;
            populateCorrespondingForks(joinNode, PSJoinNode);
            changed |= connectJoins(joinNode, PSJoinNode);
        }
    }
    return changed;
}

bool GraphBuilder::matchLocksAndUnlocks() {
    using namespace dg::analysis::pta;
    bool changed = false;
    for (auto lock : llvmToLocks_) {
        auto lockMutexPtr = pointsToAnalysis_->getPointsTo(lock.first);
        for (auto unlock : llvmToUnlocks_) {
            auto unlockMutexPtr = pointsToAnalysis_->getPointsTo(unlock.first);

            std::set<PSNode *> mutexPointerIntersection;
            for (const auto lockPointsTo : lockMutexPtr->pointsTo) {
                for (const auto unlockPointsTo : unlockMutexPtr->pointsTo) {
                    if (lockPointsTo.target == unlockPointsTo.target) {
                        mutexPointerIntersection.insert(lockPointsTo.target);
                    }
                }
            }

            if (!mutexPointerIntersection.empty()) {
                changed |= lock.second->addCorrespondingUnlock(unlock.second);
            }
        }
    }
    return changed;
}

void GraphBuilder::print(std::ostream &ostream) const {
    ostream << "digraph \"Control Flow Graph\" {\n";
    ostream << "compound = true\n";
    printNodes(ostream);
    printEdges(ostream);
    ostream << "}\n";
}

void GraphBuilder::printNodes(std::ostream &ostream) const {
    for (auto & iterator : llvmToNodeMap_) {
        ostream << iterator.second->dump();
    }
    for (auto & iterator : artificialNodes_) {
        ostream << iterator->dump();
    }
}

void GraphBuilder::printEdges(std::ostream &ostream) const {
    for (auto & iterator : llvmToNodeMap_) {
        iterator.second->printOutcomingEdges(ostream);
    }
    for (auto & iterator : artificialNodes_) {
        iterator->printOutcomingEdges(ostream);
    }
}

void GraphBuilder::clear() {
    for (auto node : artificialNodes_) {
        delete node;
    }

    for (auto llvmAndNode : llvmToNodeMap_) {
        delete llvmAndNode.second;
    }

    for (auto iterator : llvmToBlockMap_) {
        delete iterator.second;
    }

    for (auto iterator : llvmToFunctionMap_) {
        delete iterator.second;
    }

    artificialNodes_.clear();
    llvmToNodeMap_.clear();
    llvmToBlockMap_.clear();
    llvmToFunctionMap_.clear();
    llvmToJoins_.clear();
    llvmToForks_.clear();
    llvmToLocks_.clear();
    llvmToUnlocks_.clear();
}

GraphBuilder::NodeSequence GraphBuilder::buildGeneralInstruction(const Instruction *instruction) {
    auto currentNode = addNode(createNode<NodeType::GENERAL>(instruction));
    return {currentNode, currentNode};
}

GraphBuilder::NodeSequence GraphBuilder::buildGeneralCallInstruction(const CallInst *callInstruction) {
    CallNode * callNode;
    if (callInstruction->getCalledFunction()) {
        callNode = addNode(createNode<NodeType::CALL>(callInstruction));
    } else {
        callNode = addNode(createNode<NodeType::CALL>());
    }
    return {callNode, callNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertUndefinedFunction(const Function *function, const CallInst *callInstruction) {
    std::string funcName = function->getName();

    if (funcName == "pthread_create") {
        return insertPthreadCreate(callInstruction);
    } else if (funcName == "pthread_join") {
        return insertPthreadJoin(callInstruction);
    } else if (funcName == "pthread_exit") {
        return insertPthreadExit(callInstruction);
    } else if (funcName == "pthread_mutex_lock") {
        return insertPthreadMutexLock(callInstruction);
    } else if (funcName == "pthread_mutex_unlock") {
        return insertPthreadMutexUnlock(callInstruction);
    } else {
        return buildGeneralCallInstruction(callInstruction);
    }
}

GraphBuilder::NodeSequence GraphBuilder::insertPthreadCreate(const CallInst *callInstruction) {
    ForkNode * forkNode;
    if (callInstruction->getCalledFunction()) {
        forkNode = addNode(createNode<NodeType::FORK>(callInstruction));
    } else {
        forkNode = addNode(createNode<NodeType::FORK>());
    }
    auto possibleFunction = callInstruction->getArgOperand(2);
    auto functionsToBeForked = pointsToAnalysis_->getPointsToFunctions(possibleFunction);
    for (auto function : functionsToBeForked) {
        auto graph = createOrGetFunction(function);
        if (graph.first) {
            forkNode->addForkSuccessor(static_cast<EntryNode *>(graph.first));
        }
    }
    return {forkNode, forkNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertPthreadMutexLock(const CallInst *callInstruction) {
    LockNode * lockNode;
    if (callInstruction->getCalledFunction()) {
        lockNode = addNode(createNode<NodeType::LOCK>(callInstruction));
    } else {
        lockNode = addNode(createNode<NodeType::LOCK>());
    }

    return {lockNode, lockNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertPthreadMutexUnlock(const CallInst *callInstruction) {
    UnlockNode * unlockNode;
    if (callInstruction->getCalledFunction()) {
        unlockNode = addNode(createNode<NodeType::UNLOCK>(callInstruction));
    } else {
        unlockNode = addNode(createNode<NodeType::UNLOCK>());
    }

    return {unlockNode, unlockNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertPthreadJoin(const CallInst *callInstruction) {
    JoinNode * joinNode;
    if (callInstruction->getCalledFunction()) {
        joinNode = addNode(createNode<NodeType::JOIN>(callInstruction));
    } else {
        joinNode = addNode(createNode<NodeType::JOIN>());
    }
    return  {joinNode, joinNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertPthreadExit(const CallInst *callInstruction) {
    CallNode * callNode;
    if (callInstruction->getCalledFunction()) {
        callNode = addNode(createNode<NodeType::CALL>(callInstruction));
    } else {
        callNode = addNode(createNode<NodeType::CALL>());
    }
    auto returnNode = addNode(createNode<NodeType::RETURN>());
    callNode->addSuccessor(returnNode);
    return {callNode, returnNode};
}

GraphBuilder::NodeSequence GraphBuilder::insertFunction(const Function *function, const CallInst *callInstruction) {
    if (function->size() == 0) {
        return insertUndefinedFunction(function, callInstruction);
    } else {
        Node * callNode;
        if (callInstruction->getCalledFunction()) {
            callNode = createNode<NodeType::CALL>(callInstruction);
        } else {
            callNode = createNode<NodeType::CALL>();
        }
        addNode(callNode);
        auto nodeSeq = createOrGetFunction(function);
        callNode->addSuccessor(nodeSeq.first);
        return {callNode, nodeSeq.second};
    }
}

GraphBuilder::NodeSequence GraphBuilder::insertFunctionPointerCall(const CallInst *callInstruction) {
    auto calledValue = callInstruction->getCalledValue();
    auto functions = pointsToAnalysis_->getPointsToFunctions(calledValue);

    auto callFuncPtrNode = addNode(createNode<NodeType::CALL_FUNCPTR>(callInstruction));
    Node * returnNode;

    if (functions.size() > 1) {
        returnNode = addNode(createNode<NodeType::CALL_RETURN>());
        for (auto function : functions) {
            auto nodeSeq = insertFunction(function, callInstruction);
            callFuncPtrNode->addSuccessor(nodeSeq.first);
            nodeSeq.second->addSuccessor(returnNode);
        }
    } else if (functions.size() == 1) {
        auto nodeSeq = insertFunction(functions.front(), callInstruction);
        callFuncPtrNode->addSuccessor(nodeSeq.first);
        returnNode = nodeSeq.second;
    } else {
        auto nodeSeq = buildGeneralCallInstruction(callInstruction);
        callFuncPtrNode->addSuccessor(nodeSeq.first);
        returnNode = nodeSeq.second;
    }
    return {callFuncPtrNode, returnNode};
}

GraphBuilder::NodeSequence GraphBuilder::buildCallInstruction(const Instruction *instruction) {
    auto callInst = dyn_cast<CallInst>(instruction);
    if (callInst->isInlineAsm()) {
        return buildGeneralInstruction(instruction);
    }

    if (callInst->getCalledFunction()) {
        return insertFunction(callInst->getCalledFunction(), callInst);
    } else {
        return insertFunctionPointerCall(callInst);
    }
}

GraphBuilder::NodeSequence GraphBuilder::buildReturnInstruction(const Instruction *instruction) {
    auto currentNode = addNode(createNode<NodeType::RETURN>(instruction));
    return {currentNode, currentNode};
}

GraphBuilder::NodeSequence GraphBuilder::createOrGetFunction(const Function *function) {
    auto functionGraph = findFunction(function);
    if (functionGraph) {
        return {functionGraph->entryNode(), functionGraph->exitNode()};
    }
    return buildFunction(function);
}

bool GraphBuilder::populateCorrespondingForks(JoinNode *join, dg::analysis::pta::PSNodeJoin *PSJoin) {
    bool changed = false;
    auto PSForks = PSJoin->forks();
    for (auto PSFork : PSForks) {
        auto forkInstruction = getCallInst(PSFork);
        auto foundInstruction = findInstruction(forkInstruction);
        ForkNode * forkNode = castNode<NodeType::FORK>(foundInstruction);
        if (forkNode) {
            changed |= true;
            join->addCorrespondingFork(forkNode);
        }
    }
    return changed;
}

bool GraphBuilder::connectJoins(JoinNode *join, dg::analysis::pta::PSNodeJoin *PSJoin) {
    bool changed = false;
    for (auto & function : PSJoin->functions()) {
        auto llvmFunction = function->getUserData<llvm::Function>();
        auto functionGraph = findFunction(llvmFunction);
        if (functionGraph) {
            join->addJoinPredecessor(functionGraph->exitNode());
            changed |= true;
        }
    }
    return changed;
}

int predecessorsNumber(const BasicBlock *basicBlock) {
    auto number = std::distance(pred_begin(basicBlock), pred_end(basicBlock));
    return static_cast<int>(number);
}

int successorsNumber(const BasicBlock *basicBlock) {
    auto number = std::distance(succ_begin(basicBlock), succ_end(basicBlock));
    return static_cast<int>(number);
}

bool isReachable(const BasicBlock *basicBlock) {
    return predecessorsNumber(basicBlock) > 0 ||
           &basicBlock->getParent()->front() == basicBlock;
}
