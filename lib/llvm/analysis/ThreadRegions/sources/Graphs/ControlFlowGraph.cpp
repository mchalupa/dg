#include "ControlFlowGraph.h"
#include "FunctionGraph.h"
#include "EntryNode.h"
#include "ExitNode.h"
#include "JoinNode.h"
#include "ForkNode.h"

using namespace std;
using namespace llvm;

ControlFlowGraph::ControlFlowGraph(const llvm::Module *module,
                                   const dg::LLVMPointerAnalysis *pointsToAnalysis,
                                   const std::string &entryFunction)
    :llvmModule(module),
      pointsToAnalysis_(pointsToAnalysis),
      entryFunction(entryFunction) {}

ControlFlowGraph::~ControlFlowGraph() {
    for (auto it : llvmToFunctionGraphMap) {
        delete it.second;
    }

    for (auto it : threadRegions_) {
        delete it;
    }
}

void ControlFlowGraph::build() {
    llvmEntryFunction = llvmModule->getFunction(entryFunction);
    if (!llvmEntryFunction) {
        errs() << "Could not find " << entryFunction << " function\n";
        exit(EXIT_FAILURE);
    }

    auto iteratorAndBool = llvmToFunctionGraphMap.emplace(llvmEntryFunction,
                                                          new FunctionGraph(llvmEntryFunction, this));
    entryFunctionGraph = iteratorAndBool.first->second;
    entryFunctionGraph->build();
    connectForksWithJoins();
}

void ControlFlowGraph::traverse() { //TODO rename this method

    if (entryFunctionGraph) {
        ThreadRegion * region = new ThreadRegion(this);

        entryFunctionGraph->entryNode()->setThreadRegion(region);
        entryFunctionGraph->entryNode()->setDfsState(DfsState::DISCOVERED);
        entryFunctionGraph->entryNode()->dfsVisit();
        entryFunctionGraph->entryNode()->setDfsState(DfsState::EXAMINED);
        region->setDfsState(DfsState::EXAMINED);
    }
}

std::set<ThreadRegion *> ControlFlowGraph::threadRegions() const {
    return threadRegions_;
}

std::set<const CallInst *> ControlFlowGraph::getForks() {
    return threadForks;
}

std::set<const CallInst *> ControlFlowGraph::getJoins() {
    return threadJoins;
}

std::set<const CallInst *> ControlFlowGraph::getCorrespondingForks(const CallInst *join) {
    auto node = llvmToFunctionGraphMap.find(join->getFunction())->second->findBlock(join->getParent())->findNode(join);
    std::set<const llvm::CallInst *> llvmForks;
    if (node->isJoin()) {
        auto joinNode = static_cast<JoinNode *>(node);
        auto forks = joinNode->correspondingForks();
        for (const auto &fork : forks) {
            auto llvmValue = fork->llvmValue();
            if (isa<llvm::CallInst>(llvmValue)) {
                llvmForks.insert(static_cast<const CallInst *>(llvmValue));
            }
        }
    }
    return llvmForks;
}

std::set<const CallInst *> ControlFlowGraph::getCorrespondingJoins(const CallInst *fork) {
    auto node = llvmToFunctionGraphMap.find(fork->getFunction())->second->findBlock(fork->getParent())->findNode(fork);
    std::set<const llvm::CallInst *> llvmJoins;
    if (node->isFork()) {
        auto forkNode = static_cast<ForkNode*>(node);
        auto joins = forkNode->correspondingJoins();
        for (const auto &fork : joins) {
            auto llvmValue = fork->llvmValue();
            if (isa<llvm::CallInst>(llvmValue)) {
                llvmJoins.insert(static_cast<const CallInst *>(llvmValue));
            }
        }
    }
    return llvmJoins;
}

ostream &operator<<(ostream &ostream, ControlFlowGraph &controlFlowGraph) {
    ostream << "digraph \"Control Flow Graph\" {\n";
    ostream << "compound = true\n";

    for (const auto &region : controlFlowGraph.threadRegions_) {
        region->printNodes(ostream);
    }

    for (auto iterator : controlFlowGraph.llvmToFunctionGraphMap) {
        iterator.second->printEdges(ostream);
    }

    for (const auto &region : controlFlowGraph.threadRegions_) {
        region->printEdges(ostream);
    }

    ostream << "}\n";
    return ostream;
}

void ControlFlowGraph::connectForksWithJoins() {
    using namespace dg::analysis::pta;
    for (const CallInst *fork : threadForks) {
        auto forkPoint = pointsToAnalysis_->getPointsTo(fork->getArgOperand(0));
        for (const CallInst * join : threadJoins) {
            std::set<PSNode *> set;
            PSNode *joinPoint = pointsToAnalysis_->getPointsTo(join->getArgOperand(0))->getOperand(0);
            for (const auto forkNode : forkPoint->pointsTo) {
                for (const auto joinNode : joinPoint->pointsTo) {
                    if (joinNode.target == forkNode.target) {
                        set.insert(joinNode.target);
                    }
                }
            }
            if (!set.empty()) {
                auto functionWithFork = llvmToFunctionGraphMap.find(fork->getFunction())->second;
                auto forkNode = static_cast<ForkNode *>(functionWithFork->findBlock(fork->getParent())->findNode(fork));
                auto functions = pointsToAnalysis_->getPointsToFunctions(fork->getArgOperand(2));

                for (const auto &function : functions) {
                    auto forkFunctionIterator = llvmToFunctionGraphMap.find(function);
                    auto joinCallerFunction = llvmToFunctionGraphMap.find(join->getFunction())->second;
                    auto blockGraph = joinCallerFunction->findBlock(join->getParent());
                    auto joinNode = static_cast<JoinNode *>(blockGraph->findNode(join));
                    joinNode->addCorrespondingFork(forkNode);
                    forkFunctionIterator->second->exitNode()->addJoinSuccessor(joinNode);
                }
            }
        }
    }
}



