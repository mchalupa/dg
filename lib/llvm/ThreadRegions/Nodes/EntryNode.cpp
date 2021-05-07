#include "EntryNode.h"
#include "ForkNode.h"

using namespace std;

EntryNode::EntryNode() : Node(NodeType::ENTRY) {}

bool EntryNode::addForkPredecessor(ForkNode *forkNode) {
    if (!forkNode) {
        return false;
    }
    forkPredecessors_.insert(forkNode);
    return forkNode->forkSuccessors_.insert(this).second;
}

bool EntryNode::removeForkPredecessor(ForkNode *forkNode) {
    if (!forkNode) {
        return false;
    }
    forkPredecessors_.erase(forkNode);
    return forkNode->forkSuccessors_.erase(this);
}

const set<ForkNode *> &EntryNode::forkPredecessors() const {
    return forkPredecessors_;
}

std::set<ForkNode *> EntryNode::forkPredecessors() { return forkPredecessors_; }

size_t EntryNode::predecessorsNumber() const {
    return predecessors().size() + forkPredecessors_.size();
}
