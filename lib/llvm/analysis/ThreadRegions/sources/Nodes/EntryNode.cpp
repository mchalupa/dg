#include "EntryNode.h"
#include "ForkNode.h"

using namespace std;

EntryNode::EntryNode(ControlFlowGraph *controlFlowGraph):ArtificialNode(controlFlowGraph){}

void EntryNode::addForkPredecessor(ForkNode *forkNode) {
    forkPredecessors_.insert(forkNode);
    forkNode->forkSuccessors_.insert(this);
}

void EntryNode::removeForkPredecessor(ForkNode *forkNode) {
    forkPredecessors_.erase(forkNode);
    forkNode->forkSuccessors_.erase(this);
}

const set<ForkNode *> & EntryNode::forkPredecessors() const {
    return forkPredecessors_;
}


bool EntryNode::isEntry() const {
    return true;
}
