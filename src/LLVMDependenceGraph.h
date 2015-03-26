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

#include "DependenceGraph.h"

namespace dg {

class LLVMDependenceGraph;
class LLVMDGNode;

class LLVMDGNode : public DGNode<LLVMDependenceGraph, LLVMDGNode *>
{
    const llvm::Value *value;
    bool is_loop_header;
    // nodes defined at this node
    llvm::SmallPtrSet<LLVMDGNode *, 8> def;
    llvm::SmallPtrSet<LLVMDGNode *, 8> ptrs;
public:
    LLVMDGNode(const llvm::Value *val)
    : value(val), is_loop_header(false) {};

    const llvm::Value *getValue(void) const { return value; }
    void addActualParameters(LLVMDependenceGraph *);

    bool addDef(LLVMDGNode *d) { return def.insert(d).second; }
    bool addPtr(LLVMDGNode *p) { return ptrs.insert(p).second; }
    llvm::SmallPtrSet<LLVMDGNode *, 8>& getDefs() { return def; }
    llvm::SmallPtrSet<LLVMDGNode *, 8>& getPtrs() { return ptrs; }

    // override addSubgraph, we want to count references
    LLVMDependenceGraph *addSubgraph(LLVMDependenceGraph *);
};

class LLVMDependenceGraph : public DependenceGraph<const llvm::Value *, LLVMDGNode *>
{
public:
    LLVMDependenceGraph() :refcount(1) {}
    virtual ~LLVMDependenceGraph();

    bool build(llvm::Module *m, llvm::Function *entry = NULL);
    bool build(llvm::Function *func);
    // dependence graph can be share between more callsites that
    // has references to this graph. When destroying graph, we
    // must be sure do delete
    // XXX maybe these should be just friend methods ...
    int ref() { ++refcount; return refcount; }
    int unref();

    bool addNode(LLVMDGNode *n);

private:
    void addTopLevelDefUse();
    void addIndirectDefUse();
    void addFormalParameters();
    bool build(llvm::BasicBlock *BB, llvm::BasicBlock *pred = NULL);
    std::map<const llvm::Value *, LLVMDependenceGraph *> constructedFunctions;

    int refcount;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
