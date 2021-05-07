#include "CallNode.h"

CallNode::CallNode(const llvm::Instruction *instruction,
                   const llvm::CallInst *callInst)
        : Node(NodeType::CALL, instruction, callInst) {}
