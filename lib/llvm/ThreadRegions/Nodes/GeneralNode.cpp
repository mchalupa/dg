#include "GeneralNode.h"

GeneralNode::GeneralNode(const llvm::Instruction *instruction)
        : Node(NodeType::GENERAL, instruction) {}
