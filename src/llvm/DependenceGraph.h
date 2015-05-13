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
namespace llvmdg {

class DependenceGraph;
class Node;

/// ------------------------------------------------------------------
//  -- Node
/// ------------------------------------------------------------------
class Node : public dg::Node<DependenceGraph,
                             const llvm::Value *, Node *>
{
    // nodes defined at this node
    typedef llvm::SmallPtrSet<Node *, 8> DefsT;
    DefsT def;
    DefsT ptrs;
public:
    Node(const llvm::Value *val)
        :dg::Node<DependenceGraph, const llvm::Value *, Node *>(val)
    {}

    const llvm::Value *getValue() const
    {
        return getKey();
    }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    void addActualParameters(DependenceGraph *);

    bool addDef(Node *d) { return def.insert(d).second; }
    bool addPtr(Node *p) { return ptrs.insert(p).second; }
    DefsT& getDefs() { return def; }
    DefsT& getPtrs() { return ptrs; }
};

/// ------------------------------------------------------------------
//  -- DependenceGraph
/// ------------------------------------------------------------------
class DependenceGraph :
    public dg::DependenceGraph<const llvm::Value *, Node *>
{
public:
#ifdef ENABLE_CFG
    typedef dg::BBlock<Node *> BBlock;
#endif // ENABLE_CFG

    // free all allocated memory and unref subgraphs
    virtual ~DependenceGraph();

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
    bool buildSubgraph(Node *node);

    std::map<const llvm::Value *, DependenceGraph *> constructedFunctions;
};

} // namespace llvmdg
} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
