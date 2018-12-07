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

#include "dg/Node.h"

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

using LLVMBBlock = dg::BBlock<LLVMNode>;
using LLVMDGParameter = dg::DGParameterPair<LLVMNode>;
using LLVMDGParameters = dg::DGParameters<LLVMNode>;

/// ------------------------------------------------------------------
//  -- LLVMNode
/// ------------------------------------------------------------------
class LLVMNode : public Node<LLVMDependenceGraph, llvm::Value *, LLVMNode>
{
#if LLVM_VERSION_MAJOR >= 5
    struct LLVMValueDeleter {
        void operator()(llvm::Value *val) const {
            val->deleteValue();
        }
    };
#endif

public:
    LLVMNode(llvm::Value *val, bool owns_value = false)
        :dg::Node<LLVMDependenceGraph, llvm::Value *, LLVMNode>(val)
    {
        if (owns_value)
#if LLVM_VERSION_MAJOR >= 5
            owned_key = std::unique_ptr<llvm::Value, LLVMValueDeleter>(val);
#else
            owned_key = std::unique_ptr<llvm::Value>(val);
#endif
    }

    LLVMNode(llvm::Value *val, LLVMDependenceGraph *dg)
        : LLVMNode(val) {
        setDG(dg);
    }

    llvm::Value *getValue() const { return getKey(); }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    void addActualParameters(LLVMDependenceGraph *);
    void addActualParameters(LLVMDependenceGraph *, llvm::Function *, bool fork = false);

    bool isVoidTy() const {
        return getKey()->getType()->isVoidTy();
    }

private:

    // the owned key will be deleted with this node
#if LLVM_VERSION_MAJOR >= 5
    std::unique_ptr<llvm::Value, LLVMValueDeleter> owned_key;
#else
    std::unique_ptr<llvm::Value> owned_key;
#endif
};

} // namespace dg

#endif // _LLVM_NODE_H_
