// XXX license

#include "DependenceGraph.h"

#include <llvm/Support/raw_ostream.h> // errs()
#include <llvm/Function.h>
#include <llvm/BasicBlock.h>

#define DEBUG

using llvm::errs;

namespace llvm {
namespace dg {

/// --------------------------------------------------------
//    --- DGNode
/// --------------------------------------------------------
void DGNode::dump(void) const
{
	// print this node
	errs() << Value << "\n";

	// print children
	errs() << "\tControl:\n";
	for (DGNode *succ : CSuccs)
		errs() << "\t" << succ->Value << "\n";

	errs() << "\tDependencies:\n";
	for (DGNode *succ : DSuccs)
		errs() << "\t" << succ->Value << "\n";

	errs() << "\tDepends on:\n";
	for (DGNode *pred : DPreds)
		errs() << "\t" << pred->Value << "\n";
}

/// --------------------------------------------------------
//    --- DependenceGraph
/// --------------------------------------------------------
DependenceGraph::DependenceGraph(const llvm::Module *m,
                                 const llvm::Value *entry,
                                 const llvm::Value *from)
{
#ifdef DEBUG
    errs() << "Building dependence graph from ";
    if (entry)
        errs() << from;
    else
        errs() << "entry";

	errs() << "\n";

	build(m, from, entry);
#endif // DEBUG

	/*
	for (llvm::Module::const_iterator MI = m->begin(), ME = m->end();
		 MI != ME; ++MI) {
		 /*
		 if (doesNotAccessMemory())
			skip it
			*/
	}
}

DependenceGraph::DependenceGraph(const llvm::Function *func)
{
#ifdef DEBUG
    errs() << "Building dependence graph for function " << func << "\n";
#endif // DEBUG

	// iterate over basic blocks in func
	for (auto I = func->begin(), E = func->end(); I != E; ++I) {
		for (auto Inst = I->begin(), EInst = I->end();
			 Inst != EInst; ++Inst) {
			errs() << *Inst;
		}
	}
}

void DependenceGraph::build(const llvm::Module *m,
							const llvm::Value *entry,
							const llvm::Value *from)
{

}

//if file is NULL, then print to stderr otherwise print
//in .dot format to the file
void DependenceGraph::dump(const char *file) const
{
	for (auto node : Nodes) {
		node.second->dump();
	}
}

} // namespace dg
} // namespace llvm
