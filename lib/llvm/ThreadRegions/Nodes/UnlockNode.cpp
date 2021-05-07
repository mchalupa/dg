#include "UnlockNode.h"

UnlockNode::UnlockNode(const llvm::Instruction *instruction,
                       const llvm::CallInst *callInst)
        : Node(NodeType::UNLOCK, instruction, callInst) {}
