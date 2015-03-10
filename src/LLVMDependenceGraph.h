/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <map>

#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/ADT/SmallPtrSet.h>

namespace llvm {
namespace dg {

class DependenceGraph;

// one node in DependenceGraph
class DGNode
{
public:
	DGNode(const llvm::Value *v,
		   DGNode *pred = NULL,
		   DGNode *succ = NULL)
		: Value(v)
	{
		if (pred)
			CPreds.insert(pred);
		if (succ)
			CSuccs.insert(succ);
	}

private:
	friend class DependenceGraph;

	void dump(void) const;

	bool addSuccessorC(DGNode *succ) { return CSuccs.insert(succ); }
	bool addPredcessorC(DGNode *pred) { return CPreds.insert(pred); }
	bool addSuccessorD(DGNode *succ) { return DSuccs.insert(succ); }
	bool addPredcessorD(DGNode *pred) { return DPreds.insert(pred); }

	const llvm::Value *Value;
	DependenceGraph *SubDG; // set if Value is CallInst

	llvm::SmallPtrSet<DGNode *, 2> CPreds, CSuccs; // control edges
	llvm::SmallPtrSet<DGNode *, 16> DPreds, DSuccs; // dependency edges
};


// DependenceGraph
class DependenceGraph
{
public:
    // build dependence graph for module, starting at value 'entry'
    // and if 'from' is specified, build it from that instruction
    // to entry instruction (backward)
    DependenceGraph(const llvm::Module *m,
                    const llvm::Value *entry = NULL,
                    const llvm::Value *from = NULL);

    DependenceGraph(const llvm::Function *func);
    
    bool slice(DGNode *from);
    void dump(const char *file = NULL) const;
private:
    bool add(DGNode *node, DGNode *pred, DGNode *succ);
    bool remove(DGNode *node);
	void build(const llvm::Module *m,
               const llvm::Value *entry,
               const llvm::Value *from);

    std::map<const llvm::Value *, DGNode *> Nodes;
};

} // namespace dg
} // namespace llvm

#endif // _DEPENDENCE_GRAPH_H_
