/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
//#include <llvm/ADT/SmallPtrSet.h>

#include "DependenceGraph.h"

namespace dg {

class LLVMDependenceGraph;
class LLVMDGNode;

class LLVMDGNode : public DGNode<LLVMDependenceGraph, LLVMDGNode *>
{
    const llvm::Value *value;
    bool is_loop_header;
public:
    LLVMDGNode(const llvm::Value *val)
    : value(val), is_loop_header(false) {};

    const llvm::Value *getValue(void) const { return value; }
    bool isLoopHeader() const { return is_loop_header; }
    void setIsLoopHeader() { is_loop_header = true; }
};

class LLVMDependenceGraph : public DependenceGraph<const llvm::Value *, LLVMDGNode *>
{
public:
    virtual ~LLVMDependenceGraph();
    bool build(llvm::Module *m, llvm::Function *entry = NULL);
    bool build(llvm::Function *func);
    bool addNode(LLVMDGNode *n)
    { return DependenceGraph<const llvm::Value *, LLVMDGNode *>::addNode(n->getValue(), n); }

private:
    void addTopLevelDefUse();
    bool build(llvm::BasicBlock *BB, llvm::BasicBlock *pred = NULL);
    std::map<const llvm::Value *, LLVMDependenceGraph *> constructedFunctions;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
