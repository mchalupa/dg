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
class LLVMDGBasicBlock;
class LLVMDGNode;

/// ------------------------------------------------------------------
//  -- LLVMDGNode
/// ------------------------------------------------------------------
class LLVMDGNode : public DGNode<LLVMDependenceGraph, LLVMDGNode *>
{
    const llvm::Value *value;

    // nodes defined at this node
    typedef llvm::SmallPtrSet<LLVMDGNode *, 8> DefsT;
    DefsT def;
    DefsT ptrs;
public:
    LLVMDGNode(const llvm::Value *val): value(val) {};

    const llvm::Value *getValue(void) const { return value; }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    void addActualParameters(LLVMDependenceGraph *);

    bool addDef(LLVMDGNode *d) { return def.insert(d).second; }
    bool addPtr(LLVMDGNode *p) { return ptrs.insert(p).second; }
    DefsT& getDefs() { return def; }
    DefsT& getPtrs() { return ptrs; }

    // override addSubgraph, we need to count references
    LLVMDependenceGraph *addSubgraph(LLVMDependenceGraph *);
};

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------
class LLVMDependenceGraph :
    public DependenceGraph<const llvm::Value *, LLVMDGNode *>
{
public:
#ifdef ENABLE_CFG
    typedef DGBasicBlock<LLVMDGNode *> LLVMDGBasicBlock;
#endif // ENABLE_CFG

    LLVMDependenceGraph() :refcount(1) {}

    // free all allocated memory and unref subgraphs
    virtual ~LLVMDependenceGraph();

    // build a LLVMDependenceGraph from module. This method will
    // build all subgraphs (called procedures). If entry is NULL,
    // then this methods looks for function named 'main'.
    bool build(llvm::Module *m, llvm::Function *entry = NULL);

    // build LLVMDependenceGraph for a function. This will automatically
    // build subgraphs of called functions
    bool build(llvm::Function *func);

    // dependence graph can be shared between more call-sites that
    // has references to this graph. When destroying graph, we
    // must be sure do delete it just once, so count references
    int ref() { ++refcount; return refcount; }
    // unref graph and delete if refrences drop to 0
    // destructor calls this on subgraphs
    int unref();

    // add a node to this graph. The DependenceGraph is something like
    // namespace for nodes, since every node has unique key and we can
    // have another node with same key (for the same llvm::Value) in another
    // graph. So we can have two nodes for the same value but in different
    // graphs. The edges can be between arbitrary nodes and do not
    // depend on graphs the nodes are.
    bool addNode(LLVMDGNode *n);
private:
    // fill in def-use chains that we have from llvm
    void addTopLevelDefUse();
    void addIndirectDefUse();
    void addPostDomTree();

    // add formal parameters of the function to the graph
    // (graph is a graph of one procedure)
    void addFormalParameters();

    bool build(llvm::BasicBlock *BB, llvm::BasicBlock *pred = NULL);

    // build subgraph for a call node
    bool buildSubgraph(LLVMDGNode *node);

    std::map<const llvm::Value *, LLVMDependenceGraph *> constructedFunctions;

    int refcount;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
