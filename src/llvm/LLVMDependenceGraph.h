/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/ADT/SmallPtrSet.h>

#include "DependenceGraph.h"
#include "LLVMNode.h"

namespace dg {

typedef dg::BBlock<LLVMNode> LLVMBBlock;

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------
class LLVMDependenceGraph : public dg::DependenceGraph<LLVMNode>
{
public:
    // free all allocated memory and unref subgraphs
    virtual ~LLVMDependenceGraph();

    // build a DependenceGraph from module. This method will
    // build all subgraphs (called procedures). If entry is nullptr,
    // then this methods looks for function named 'main'.
    bool build(llvm::Module *m, llvm::Function *entry = nullptr);

    // build DependenceGraph for a function. This will automatically
    // build subgraphs of called functions
    bool build(llvm::Function *func);

private:
    // add formal parameters of the function to the graph
    // (graph is a graph of one procedure)
    void addFormalParameters();

    bool build(llvm::BasicBlock *BB, llvm::BasicBlock *pred = nullptr);

    // build subgraph for a call node
    bool buildSubgraph(LLVMNode *node);

    std::map<const llvm::Value *, LLVMDependenceGraph *> constructedFunctions;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
