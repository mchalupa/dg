#ifndef LLVM_NODE_H_
#define LLVM_NODE_H_

#include <map>
#include <set>
#include <utility>

#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

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
class LLVMNode : public Node<LLVMDependenceGraph, llvm::Value *, LLVMNode> {
#if LLVM_VERSION_MAJOR >= 5
    struct LLVMValueDeleter {
        void operator()(llvm::Value *val) const { val->deleteValue(); }
    };
#endif

  public:
    LLVMNode(llvm::Value *val, bool owns_value = false)
            : dg::Node<LLVMDependenceGraph, llvm::Value *, LLVMNode>(val) {
        if (owns_value)
#if LLVM_VERSION_MAJOR >= 5
            owned_key = std::unique_ptr<llvm::Value, LLVMValueDeleter>(val);
#else
            owned_key = std::unique_ptr<llvm::Value>(val);
#endif
    }

    LLVMNode(llvm::Value *val, LLVMDependenceGraph *dg) : LLVMNode(val) {
        setDG(dg);
    }

    LLVMDGParameters *getOrCreateParameters() {
        auto *params = getParameters();
        if (!params) {
            params = new LLVMDGParameters(this);
            setParameters(params);
        }
        return params;
    }

    llvm::Value *getValue() const { return getKey(); }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    void addActualParameters(LLVMDependenceGraph * /*funcGraph*/);
    void addActualParameters(LLVMDependenceGraph * /*funcGraph*/,
                             llvm::Function * /*func*/, bool fork = false);

    bool isVoidTy() const { return getKey()->getType()->isVoidTy(); }

  private:
    // the owned key will be deleted with this node
#if LLVM_VERSION_MAJOR >= 5
    std::unique_ptr<llvm::Value, LLVMValueDeleter> owned_key;
#else
    std::unique_ptr<llvm::Value> owned_key;
#endif
};

} // namespace dg

#endif // LLVM_NODE_H_
