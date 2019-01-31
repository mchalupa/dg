#include "ThreadRegion.h"
#include "ControlFlowGraph.h"
#include "Node.h"
#include "DfsState.h"

int ThreadRegion::lastId = 0;

ThreadRegion::ThreadRegion(ControlFlowGraph *controlFlowGraph):id_(lastId++),
                                                               controlFlowGraph_(controlFlowGraph) {
    controlFlowGraph_->threadRegions_.insert(this);
}

ControlFlowGraph *ThreadRegion::controlFlowGraph() {
    return controlFlowGraph_;
}

int ThreadRegion::id() const {
    return id_;
}

bool ThreadRegion::addPredecessor(ThreadRegion *predecessor) {
    predecessors_.insert(predecessor);
    return predecessor->successors_.insert(this).second;
}

bool ThreadRegion::addSuccessor(ThreadRegion *threadRegion) {
    successors_.insert(threadRegion);
    return threadRegion->predecessors_.insert(this).second;
}

bool ThreadRegion::removePredecessor(ThreadRegion *predecessor) {
    if (!predecessor) {
        return false;
    }
    predecessors_.erase(predecessor);
    return predecessor->successors_.erase(this);
}

bool ThreadRegion::removeSuccessor(ThreadRegion *successor) {
    if (!successor) {
        return false;
    }
    successors_.erase(successor);
    return successor->predecessors_.erase(this);
}

const std::set<ThreadRegion *> &ThreadRegion::predecessors() const {
    return predecessors_;
}

std::set<ThreadRegion *> ThreadRegion::predecessors() {
    return predecessors_;
}

const std::set<ThreadRegion *> &ThreadRegion::successors() const {
    return successors_;
}

std::set<ThreadRegion *> ThreadRegion::successors() {
    return successors_;
}

bool ThreadRegion::insertNode(Node *node) {
    nodes_.insert(node);
    return false;
}

bool ThreadRegion::removeNode(Node *node) {
    nodes_.erase(node);
    return false;
}

const std::set<Node *> &ThreadRegion::nodes() const {
    return nodes_;
}

std::set<Node *> ThreadRegion::nodes() {
    return nodes_;
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
