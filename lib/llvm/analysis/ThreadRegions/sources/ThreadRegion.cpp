#include "ThreadRegion.h"
#include "ControlFlowGraph.h"
#include "Node.h"
#include "LlvmNode.h"
#include "DfsState.h"

int ThreadRegion::lastId = 0;

ThreadRegion::ThreadRegion(ControlFlowGraph *controlFlowGraph):id_(lastId++),
                                                               controlFlowGraph_(controlFlowGraph) {
    controlFlowGraph_->threadRegions_.insert(this);
}

ControlFlowGraph *ThreadRegion::controlFlowGraph() {
    return controlFlowGraph_;
}

void ThreadRegion::addPredecessor(ThreadRegion *predecessor) {
    predecessors_.insert(predecessor);
    predecessor->successors_.insert(this);
}

void ThreadRegion::addSuccessor(ThreadRegion *threadRegion) {
    successors_.insert(threadRegion);
    threadRegion->predecessors_.insert(this);
}

void ThreadRegion::removePredecessor(ThreadRegion *predecessor) {
    predecessors_.equal_range(predecessor);
    predecessor->successors_.erase(this);
}

void ThreadRegion::removeSuccessor(ThreadRegion *successor) {
    successors_.erase(successor);
    successor->predecessors_.erase(this);
}

void ThreadRegion::insertNode(Node *node) {
    nodes_.insert(node);
}

void ThreadRegion::removeNode(Node *node) {
    nodes_.erase(node);
}

void ThreadRegion::printNodes(std::ostream &ostream) {
    ostream << "subgraph " << dotName() << " {\n";
    ostream << "color = blue\n style = rounded\n";
    for (const auto &node : nodes_) {
        ostream << node->dump();
    }
    ostream << "}\n";
}

void ThreadRegion::printEdges(std::ostream &ostream) {
    for (const auto &successor : successors_) {
        ostream << (*this->nodes_.begin())->dotName()
                << " -> "
                << (*successor->nodes_.begin())->dotName()
                << " [ltail = " << this->dotName()
                << " lhead = " << successor->dotName()
                << ", color = blue, style = bold]\n";
    }
}

DfsState ThreadRegion::dfsState() const {
    return dfsState_;
}

void ThreadRegion::setDfsState(DfsState dfsState) {
    dfsState_ = dfsState;
}

std::string ThreadRegion::dotName() {
    return "cluster" + std::to_string(id_);
}

std::set<const llvm::Value *> ThreadRegion::llvmValues() const {
    std::set<const llvm::Value *> llvmValues;
    for (const auto &node : nodes_) {
        if (!node->isArtificial()) {
            const LlvmNode *llvmNode = static_cast<const LlvmNode *>(node);
            llvmValues.insert(llvmNode->llvmValue());
        }
    }
    return llvmValues;
}
