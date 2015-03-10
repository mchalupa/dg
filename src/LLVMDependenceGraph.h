/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/ADT/SmallPtrSet.h>

#include "DependenceGraph.h"

namespace dg {

class LLVMDGNode : public dg::DGNode<const llvm::Value *>
{
	const llvm::Value *getValue(void) const { return getKey(); }
};

class LLVMDependencyGraph : public dg::DependenceGraph<const llvm::Value *>
{
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_

#endif /* HAVE_LLVM */
