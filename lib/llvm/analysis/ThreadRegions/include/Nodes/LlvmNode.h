#ifndef LLVMNODE_H
#define LLVMNODE_H

#include "Node.h"

namespace llvm {
    class Value;
}

class LlvmNode : public Node
{
    const llvm::Value * llvmValue_ = nullptr;
public:
    LlvmNode(ControlFlowGraph * controlFlowGraph, const llvm::Value * value);

    std::string dump() const override;

    const llvm::Value * llvmValue() const;
};

#endif // LLVMNODE_H
