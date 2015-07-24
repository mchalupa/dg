/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/ADT/SmallPtrSet.h>

#include "../DependenceGraph.h"

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

typedef dg::BBlock<LLVMNode *> LLVMBBlock;

typedef dg::DGParameter<LLVMNode *> LLVMDGParameter;
typedef dg::DGParameters<const llvm::Value *, LLVMNode *> LLVMDGParameters;

/// ------------------------------------------------------------------
//  -- LLVMNode
/// ------------------------------------------------------------------
class LLVMNode : public dg::Node<LLVMDependenceGraph,
                                 const llvm::Value *, LLVMNode *>
{
public:
    LLVMNode(const llvm::Value *val)
        :dg::Node<LLVMDependenceGraph, const llvm::Value *, LLVMNode *>(val)
    {}

    const llvm::Value *getValue() const
    {
        return getKey();
    }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    // XXX create new class for parameters
    void addActualParameters(LLVMDependenceGraph *);
};

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------
class LLVMDependenceGraph :
    public dg::DependenceGraph<const llvm::Value *, LLVMNode *>
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
