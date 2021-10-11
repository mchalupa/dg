#include "ExitNode.h"
#include "JoinNode.h"

#include <iostream>
#include <string>

ExitNode::ExitNode() : Node(NodeType::EXIT) {}

bool ExitNode::addJoinSuccessor(JoinNode *joinNode) {
    if (!joinNode) {
        return false;
    }
    joinSuccessors_.insert(joinNode);
    return joinNode->joinPredecessors_.insert(this).second;
}

bool ExitNode::removeJoinSuccessor(JoinNode *joinNode) {
    if (!joinNode) {
        return false;
    }
    joinSuccessors_.erase(joinNode);
    return joinNode->joinPredecessors_.erase(this);
}

const std::set<JoinNode *> &ExitNode::joinSuccessors() const {
    return joinSuccessors_;
}

std::set<JoinNode *> ExitNode::joinSuccessors() { return joinSuccessors_; }

std::size_t ExitNode::successorsNumber() const {
    return successors().size() + joinSuccessors_.size();
}

void ExitNode::printOutcomingEdges(std::ostream &ostream) const {
    Node::printOutcomingEdges(ostream);
    for (const auto &joinSuccessor : joinSuccessors_) {
        ostream << this->dotName() << " -> " << joinSuccessor->dotName()
                << " [style=dashed]\n";
    }
}
