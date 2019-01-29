#include "BlockGraph.h"
#include "Node.h"
#include "EndifNode.h"
#include "ForkNode.h"
#include "JoinNode.h"
#include "ReturnNode.h"
#include "EntryNode.h"
#include "ExitNode.h"
#include "ControlFlowGraph.h"
#include "FunctionGraph.h"

#include <llvm/IR/CFG.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

BlockGraph::BlockGraph(const BasicBlock *llvmBlock, ControlFlowGraph *controlFlowGraph)
    :controlFlowGraph(controlFlowGraph),
     llvmBlock_(llvmBlock){}

BlockGraph::~BlockGraph() {
    for (auto & node : allNodes) {
        delete node;
    }
}

Node *BlockGraph::firstNode() const {
    return firstNode_;
}

Node *BlockGraph::lastNode() const {
    return lastNode_;
}

const llvm::BasicBlock *BlockGraph::llvmBlock() const {
    return llvmBlock_;
}

bool BlockGraph::hasStructure() const {
    return hasStructure_;
}

void BlockGraph::build() {
    if (predecessorsNumber() > 1) {
        firstNode_ = new EndifNode(controlFlowGraph);
        addNode(static_cast<EndifNode *>(firstNode_));
    }

    Node * lastConnectedNode = firstNode_;

    for (const Instruction &instruction : *llvmBlock_) {
        switch (instruction.getOpcode()) {
        case Instruction::Call:
        {
            const CallInst *callInstruction = dyn_cast<CallInst>(&instruction);
            buildCallInstruction(callInstruction, lastConnectedNode);
            break;
        }
        default:
            buildGeneralNode(&instruction, lastConnectedNode);
            break;
        }
    }
    lastNode_ = lastConnectedNode;
}

void BlockGraph::printNodes(std::ostream &ostream) const {
    for (auto &node : allNodes) {
        ostream << node->dump();
    }
}

void BlockGraph::printEdges(std::ostream &ostream) const {
    for (auto &node : allNodes) {
        node->printOutcomingEdges(ostream);
    }
}

int BlockGraph::predecessorsNumber() const {
    int predecessorsNumber = 0;
    for (auto iterator = pred_begin(llvmBlock_); iterator != pred_end(llvmBlock_); ++iterator ) {
        predecessorsNumber++;
    }
    return predecessorsNumber;
}

void BlockGraph::addNode(ArtificialNode *artificialNode) {
    allNodes.insert(artificialNode);
}

void BlockGraph::addNode(LlvmNode *llvmNode) {
    allNodes.insert(llvmNode);
    llvmToNodeMap.emplace(llvmNode->llvmValue(), llvmNode);
}

void BlockGraph::buildCallInstruction(const CallInst *callInstruction, Node *&lastConnectedNode) {
    if (callInstruction->isInlineAsm()) {
        buildGeneralNode(callInstruction, lastConnectedNode);
        return;
    }

    const Value *calledValue = callInstruction->getCalledValue();

    auto pointsToFunctions = controlFlowGraph->pointsToAnalysis_->getPointsToFunctions(calledValue);

    if (pointsToFunctions.empty()) {
        buildGeneralNode(callInstruction, lastConnectedNode);
        return;
    }

    Node *callNode = nullptr;
    Node *returnNode = nullptr;

    const Function *pthreadCreate = didContainFunction(pointsToFunctions, "pthread_create"); 
    const Function *pthreadJoin = didContainFunction(pointsToFunctions, "pthread_join");
    const Function *pthreadLock = didContainFunction(pointsToFunctions, "pthread_mutex_lock");
    const Function *pthreadUnlock = didContainFunction(pointsToFunctions, "pthread_mutex_unlock");
    
    if (pthreadCreate) {
        callNode = buildPthreadCreate(callInstruction); 
    } else if (pthreadJoin) {
        callNode = buildPthreadJoin(callInstruction);
    } else if (pthreadLock) {
        callNode = buildPthreadLock(callInstruction);
    } else if (pthreadUnlock) {
        callNode = buildPthreadUnlock(callInstruction);
    }
    if (!pointsToFunctions.empty()) {
        std::tie(callNode, returnNode) = buildFunctions(callInstruction, pointsToFunctions);
    }    

    if (!firstNode_) {
        firstNode_ = lastConnectedNode = callNode;
    } else {
        lastConnectedNode->addSuccessor(callNode);
    }

    if (returnNode) {
        lastConnectedNode = returnNode;
    } else {
        lastConnectedNode = callNode;
    }
}

void BlockGraph::buildGeneralNode(const Instruction *instruction, Node *&lastConnectedNode)
{
    LlvmNode *currentNode = new LlvmNode(controlFlowGraph, instruction);
    addNode(currentNode);
    if (!firstNode_) {
        firstNode_ = lastConnectedNode = currentNode;
    } else {
        lastConnectedNode->addSuccessor(currentNode);
        lastConnectedNode = currentNode;
    }
}

Node *BlockGraph::buildPthreadCreate(const llvm::CallInst * callInstruction) {
    using namespace llvm;
    controlFlowGraph->threadForks.insert(callInstruction);
    ForkNode *forkNode = new ForkNode(controlFlowGraph, callInstruction);
    addNode(forkNode);
    const Value *possibleFunction = callInstruction->getArgOperand(2);
    auto functionsToBeForked = controlFlowGraph->pointsToAnalysis_->getPointsToFunctions(possibleFunction);
    
    for (auto function : functionsToBeForked) {
        auto functionGraph = controlFlowGraph->createOrGetFunctionGraph(function);
        forkNode->addForkSuccessor(functionGraph->entryNode());
    }
    return forkNode;
}

Node *BlockGraph::buildPthreadJoin(const llvm::CallInst * callInstruction) {
    controlFlowGraph->threadJoins.insert(callInstruction);
    JoinNode *joinNode = new JoinNode(controlFlowGraph, callInstruction);
    addNode(joinNode);
    return joinNode;
}

Node *BlockGraph::buildPthreadLock(const llvm::CallInst * callInstruction) {
    controlFlowGraph->locks.insert(callInstruction);
    LockNode * lockNode = new LockNode(controlFlowGraph, callInstruction);
    addNode(lockNode);
    return lockNode;
}

Node *BlockGraph::buildPthreadUnlock(const llvm::CallInst * callInstruction) {
    controlFlowGraph->unlocks.insert(callInstruction);
    UnlockNode * unlockNode = new UnlockNode(controlFlowGraph, callInstruction);
    addNode(unlockNode);
    return unlockNode;
}

std::pair<Node *, Node *> 
BlockGraph::buildFunctions(const llvm::CallInst * callInstruction, 
                           const std::vector<const llvm::Function *> & functions) {
    Node * callNode   = findNode(callInstruction);
    if (!callNode) {
        callNode = new LlvmNode(controlFlowGraph, callInstruction);
        addNode(static_cast<LlvmNode *>(callNode));
    }
    Node * returnNode = new ReturnNode(controlFlowGraph);
    addNode(static_cast<ReturnNode *>(returnNode));

    for (auto function : functions) {
        FunctionGraph * functionGraph = controlFlowGraph->createOrGetFunctionGraph(function);
        callNode->addSuccessor(functionGraph->entryNode());
        functionGraph->exitNode()->addSuccessor(returnNode);
    }

    return {callNode, returnNode};
}

bool BlockGraph::containsReturn() const {
    return llvmBlock_->back().getOpcode() == Instruction::Ret;
}

Node *BlockGraph::findNode(const Value *value) const {
    auto iterator = llvmToNodeMap.find(value);
    if (iterator == llvmToNodeMap.end()) {
        return nullptr;
    } else {
        return iterator->second;
    }
}

void BlockGraph::clearDfsState() {
    for (auto node : allNodes) {
        node->setDfsState(DfsState::UNDISCOVERED);
    }
}

const llvm::Function * didContainFunction(std::vector<const llvm::Function *> &functions, 
                        const std::string &function) 
{    
    auto iterator = std::find_if(functions.begin(), 
                                 functions.end(), 
                                 [function] (const llvm::Function * f) { 
                                    return f->getName() == function; 
                                 });
    const llvm::Function * func = nullptr;
    if (iterator != functions.end()) {
        func = *iterator;
        functions.erase(iterator);
    }
    return func;
}
