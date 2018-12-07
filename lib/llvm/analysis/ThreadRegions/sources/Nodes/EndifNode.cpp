#include "EndifNode.h"


EndifNode::EndifNode(ControlFlowGraph *controlFlowGraph):ArtificialNode(controlFlowGraph) {}

bool EndifNode::isEndIf() const {
    return true;
}
