#include "ControlFlowGraph.h"
#include "FunctionGraph.h"
#include "Node.h"
#include "EntryNode.h"
#include "ExitNode.h"
#include "JoinNode.h"
#include "ForkNode.h"

using namespace std;
using namespace llvm;

CriticalSection::CriticalSection():lock(nullptr) 
{}

CriticalSection::CriticalSection(const llvm::Value * lock,
                                 std::set<const llvm::Value *> &&joins,
                                 std::set<const llvm::Value *> &&nodes):lock(lock),
                                                                        unlocks(joins),
                                                                        nodes(nodes){}

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

void ControlFlowGraph::computeThreadRegions() { 
    if (entryFunctionGraph) {
        ThreadRegion * region = new ThreadRegion(this);

        entryFunctionGraph->entryNode()->setThreadRegion(region);
        entryFunctionGraph->entryNode()->setDfsState(DfsState::DISCOVERED);
        entryFunctionGraph->entryNode()->dfsComputeThreadRegions();
        entryFunctionGraph->entryNode()->setDfsState(DfsState::EXAMINED);
        region->setDfsState(DfsState::EXAMINED);
    }
    clearDfsState();
}

void ControlFlowGraph::computeCriticalSections() {
    matchLocksWithUnlocks();
    for (auto callInstLock : locks) {
        LockNode * lock = static_cast<LockNode *>(findNode(callInstLock));
        lock->setDfsState(DfsState::DISCOVERED);
        lock->dfsComputeCriticalSections(lock);
        clearDfsState();
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

std::set<CriticalSection> ControlFlowGraph::getCriticalSections() {
    std::set<CriticalSection> critSections;
    for (auto lock : locks) {
        auto lockNode = static_cast<LockNode *>(findNode(lock));
        critSections.emplace(lock, lockNode->llvmUnlocks(), lockNode->llvmCriticalSectioon());
    }
    return critSections;
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

void ControlFlowGraph::matchLocksWithUnlocks() {
    using namespace dg::analysis::pta;

    for (const CallInst *lock : locks) {
        auto lockMutexPtr = pointsToAnalysis_->getPointsTo(lock->getArgOperand(0));
        for (const CallInst *unlock : unlocks) {
            auto unlockMutexPtr = pointsToAnalysis_->getPointsTo(unlock->getArgOperand(0));
            
            std::set<PSNode *> mutexPointerIntersection;
            for (const auto lockPointsTo : lockMutexPtr->pointsTo) {
                for (const auto unlockPointsTo : unlockMutexPtr->pointsTo) {
                    if (lockPointsTo.target == unlockPointsTo.target) {
                        mutexPointerIntersection.insert(lockPointsTo.target);
                    }
                }
            }
            if (!mutexPointerIntersection.empty()) {
                LockNode *lockNode = static_cast<LockNode *>(findNode(lock));
                UnlockNode *unlockNode = static_cast<UnlockNode *>(findNode(unlock));
                lockNode->addCorrespondingUnlock(unlockNode);
            }
        }
    }
}

FunctionGraph * ControlFlowGraph::createOrGetFunctionGraph(const llvm::Function * function) {
    FunctionGraph *functionGraph;
    auto iterator = llvmToFunctionGraphMap.find(function);
    if (iterator == llvmToFunctionGraphMap.end()) {
        auto iteratorAndBool = llvmToFunctionGraphMap.emplace(function, new FunctionGraph(function, this));
        functionGraph = iteratorAndBool.first->second;
        functionGraph->build();
    } else {
        functionGraph = iterator->second;
    }
    return functionGraph;
}

FunctionGraph * ControlFlowGraph::findFunction(const llvm::Function * function) {
    auto iterator = llvmToFunctionGraphMap.find(function);
    if (iterator == llvmToFunctionGraphMap.end()) {
        return nullptr;
    } else {
        return iterator->second;
    }
}

Node * ControlFlowGraph::findNode(const llvm::Value * value) {
    Node * node = nullptr;
    const llvm::Instruction *inst = dyn_cast<llvm::Instruction>(value);
    auto function = findFunction(inst->getFunction());
    if (function) {
        node = function->findNode(value);
    }
    return node;
}

void ControlFlowGraph::clearDfsState() {
    for (auto keyValue : llvmToFunctionGraphMap) {
        keyValue.second->clearDfsState();
    }
}


