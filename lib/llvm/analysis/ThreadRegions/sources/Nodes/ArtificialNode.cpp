#include "ArtificialNode.h"

using namespace std;

ArtificialNode::ArtificialNode(ControlFlowGraph *controlFlowGraph):Node(controlFlowGraph) {}

bool ArtificialNode::isArtificial() const {
    return true;
}


std::string ArtificialNode::dump() const {
    return this->dotName() + " [label=\"<" + to_string(this->id()) + ">" + " Artifical node\"]\n";
}
