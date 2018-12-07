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
            LlvmNode *currentNode = new LlvmNode(controlFlowGraph, &instruction);
            addNode(currentNode);
            if (!firstNode_) {
                firstNode_ = lastConnectedNode = currentNode;
            } else {
                lastConnectedNode->addSuccessor(currentNode);
                lastConnectedNode = currentNode;
            }
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
    Node *callNode = nullptr;
    Node *returnNode = nullptr;

    const Value *calledValue = callInstruction->getCalledValue()->stripPointerCasts();
    auto pointsToFunctions = controlFlowGraph->pointsToAnalysis_->getPointsToFunctions(calledValue);

    const Function *pthreadCreate = nullptr;
    const Function *pthreadJoin = nullptr;
    for (auto iterator = pointsToFunctions.begin(); iterator != pointsToFunctions.end(); ++iterator) {
        if ((*iterator)->getName() == "pthread_create") {
            pthreadCreate = *iterator;
            iterator = pointsToFunctions.erase(iterator);
            if (iterator == pointsToFunctions.end()) {
                break;
            }
        }
        if ((*iterator)->getName() == "pthread_join") {
            pthreadJoin = *iterator;
            iterator = pointsToFunctions.erase(iterator);
            if (iterator == pointsToFunctions.end()) {
                break;
            }
        }
    }

    if (pthreadCreate) {
        controlFlowGraph->threadForks.insert(callInstruction);
        ForkNode *forkNode = new ForkNode(controlFlowGraph, callInstruction);
        callNode = forkNode;
        addNode(forkNode);
        const Value *possibleFunction = callInstruction->getArgOperand(2);
        auto functionsToBeForked = controlFlowGraph->pointsToAnalysis_->getPointsToFunctions(possibleFunction);
        FunctionGraph * functionGraph;
        for (auto function : functionsToBeForked) {
            auto iterator = controlFlowGraph->llvmToFunctionGraphMap.find(function);
            if (iterator == controlFlowGraph->llvmToFunctionGraphMap.end()) {
                auto iteratorAndBool = controlFlowGraph->llvmToFunctionGraphMap.emplace(function,
                                                                                        new FunctionGraph(function, controlFlowGraph));
                functionGraph = iteratorAndBool.first->second;
                functionGraph->build();
            } else {
                functionGraph = iterator->second;
            }
            forkNode->addForkSuccessor(functionGraph->entryNode());
        }
    } else if (pthreadJoin) {
        controlFlowGraph->threadJoins.insert(callInstruction);
        JoinNode *joinNode = new JoinNode(controlFlowGraph, callInstruction);
        callNode = joinNode;
        addNode(joinNode);
    }
    if (!pointsToFunctions.empty()) {
        callNode = new LlvmNode(controlFlowGraph, callInstruction);
        returnNode = new ReturnNode(controlFlowGraph);
        addNode(static_cast<LlvmNode *>(callNode));
        addNode(static_cast<ReturnNode *>(returnNode));
    }

    for (const Function *function : pointsToFunctions) {
        FunctionGraph *functionGraph;
        auto iterator = controlFlowGraph->llvmToFunctionGraphMap.find(function);
        if (iterator == controlFlowGraph->llvmToFunctionGraphMap.end()) {
            auto iteratorAndBool = controlFlowGraph->llvmToFunctionGraphMap.emplace(function,
                                                                                    new FunctionGraph(function, controlFlowGraph));
            functionGraph = iteratorAndBool.first->second;
            functionGraph->build();
        } else {
            functionGraph = iterator->second;
        }
        callNode->addSuccessor(functionGraph->entryNode());
        functionGraph->exitNode()->addSuccessor(returnNode);
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

