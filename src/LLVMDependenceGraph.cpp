/// XXX add licence
//

#ifdef HAVE_LLVM

/*
#include <llvm/Function.h>
#include <llvm/ADT/SmallPtrSet.h>
*/
#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDependenceGraph.h"

using llvm::errs;

namespace dg {

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    for (auto I = begin(), E = end(); I != E; ++I)
        delete I->second;
}

bool LLVMDependenceGraph::build(llvm::Module *m, llvm::Function *entry)
{
    // get entry function if not given
    if (!entry)
        entry = m->getFunction("main");

    if (!entry) {
        errs() << "No entry function found/given\n";
        return false;
    }

    // iterate over functions and create PDGs
    for (auto I = m->begin(), E = m->end(); I != E; ++I) {
        // we'll handle entry separately after we create PDGs for
        // every function
        if (&*I == entry)
            continue;

        build(&*I);
    }
};

bool LLVMDependenceGraph::build(llvm::Function *func)
{
    for (auto BB = func->begin(), BE = func->end(); BB != BE; ++BB) {
        for (auto Inst = BB->begin(), EInst = BB->end(); Inst != EInst; ++Inst) {
            LLVMDGNode *node = new LLVMDGNode(&*Inst);
            addNode(node);
        }
    }
}

} // namespace dg

#endif /* HAVE_LLVM */
