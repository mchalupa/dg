#include "ExitNode.h"
#include "JoinNode.h"
#include "ThreadRegion.h"

#include <iostream>

ExitNode::ExitNode(ControlFlowGraph *controlFlowGraph):ArtificialNode(controlFlowGraph) {}

void ExitNode::addJoinSuccessor(JoinNode *joinNode) {
    joinSuccessors_.insert(joinNode);
    joinNode->joinPredecessors_.insert(this);
}

void ExitNode::removeJoinSuccessor(JoinNode *joinNode) {
    joinSuccessors_.erase(joinNode);
    joinNode->joinPredecessors_.erase(this);
}

const std::set<JoinNode *> &ExitNode::joinSuccessors() const {
    return joinSuccessors_;
}


void ExitNode::printOutcomingEdges(std::ostream &ostream) const {
    Node::printOutcomingEdges(ostream);
    for (const auto &joinSuccessor : joinSuccessors_) {
        ostream << this->dotName() << " -> " << joinSuccessor->dotName() << " [style=dashed]\n";
    }
}


void ExitNode::dfsComputeThreadRegions() {
    computeThreadRegionsOnSuccessorsFromNode(joinSuccessors(), this);
    computeThreadRegionsOnSuccessorsFromNode(successors(), this);
}

void ExitNode::dfsComputeCriticalSections(LockNode *lock) {
    computeCriticalSectionsDependentOnLock(joinSuccessors(), lock);
    computeCriticalSectionsDependentOnLock(successors(), lock);
}

bool ExitNode::isExit() const {
    return true;
}
