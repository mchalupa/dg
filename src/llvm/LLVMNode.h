#ifndef _LLVM_NODE_H_
#define _LLVM_NODE_H_

#ifndef HAVE_LLVM
#error "Need LLVM"
#endif

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

#include <utility>
#include <map>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "Node.h"

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

using LLVMBBlock = dg::BBlock<LLVMNode>;
using LLVMDGParameter = dg::DGParameter<LLVMNode>;
using LLVMDGParameters = dg::DGParameters<LLVMNode>;

/// ------------------------------------------------------------------
//  -- LLVMNode
/// ------------------------------------------------------------------
class LLVMNode : public Node<LLVMDependenceGraph, llvm::Value *, LLVMNode>
{
public:
    LLVMNode(llvm::Value *val, LLVMDependenceGraph *dg = nullptr)
        :dg::Node<LLVMDependenceGraph, llvm::Value *, LLVMNode>(val, dg) {}

    ~LLVMNode();

    llvm::Value *getValue() const { return getKey(); }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    // XXX create new class for parameters
    void addActualParameters(LLVMDependenceGraph *);
    void addActualParameters(LLVMDependenceGraph *, llvm::Function *);

    LLVMNode **getOperands();
    size_t getOperandsNum();
    LLVMNode *getOperand(unsigned int idx);
    LLVMNode *setOperand(LLVMNode *op, unsigned int idx);

    bool isVoidTy() const {
        return getKey()->getType()->isVoidTy();
    }

private:
    LLVMNode **findOperands();
    // here we can store operands of instructions so that
    // finding them will be asymptotically constant
    LLVMNode **operands{nullptr};
    size_t operands_num{0};
};

} // namespace dg

#endif // _LLVM_NODE_H_
