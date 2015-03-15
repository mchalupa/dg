/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
//#include <llvm/ADT/SmallPtrSet.h>

#include "DependenceGraph.h"

namespace dg {

class LLVMDGNode : public dg::DGNode<const llvm::Value *>
{
public:
    LLVMDGNode(const llvm::Value *val) : DGNode<const llvm::Value *>(val) {};
	const llvm::Value *getValue(void) const { return getKey(); }
};

class LLVMDependenceGraph : public dg::DependenceGraph<const llvm::Value *>
{
public:
    virtual ~LLVMDependenceGraph();
    bool build(llvm::Module *m, llvm::Function *entry = NULL);
    bool build(llvm::Function *func);
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
