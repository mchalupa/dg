#include "CallFuncPtrNode.h"

CallFuncPtrNode::CallFuncPtrNode(const llvm::Instruction *instruction)
        : Node(NodeType::CALL_FUNCPTR, instruction) {}
