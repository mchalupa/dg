/// XXX add licence
//

#ifdef HAVE_LLVM

#include <utility>
#include <queue>
/*
#include <llvm/Function.h>
#include <llvm/ADT/SmallPtrSet.h>
*/
#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDependenceGraph.h"

using llvm::errs;
using std::make_pair;

namespace dg {

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    for (auto I = begin(), E = end(); I != E; ++I) {
        if (I->second) {
            if (I->second->getSubgraph())
                delete I->second->getSubgraph();

            delete I->second;
        }
    }
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

    // build recursively DG from entry point
    build(entry);
};

bool LLVMDependenceGraph::build(llvm::BasicBlock *BB, llvm::BasicBlock *pred)
{
    using namespace llvm;

#ifdef ENABLE_CFG
    LLVMDGNode *predNode = NULL;
    if (pred) {
        predNode = (*this)[pred->getTerminator()];
        assert(predNode && "Predcessor node is not created");
    }
#endif //ENABLE_CFG

    for (auto Inst = BB->begin(), EInst = BB->end(); Inst != EInst; ++Inst) {
        const llvm::Value *val = &(*Inst);

        LLVMDGNode *node = new LLVMDGNode(val);
        addNode(node);

#ifdef ENABLE_CFG
        if (predNode)
            predNode->addSucc(node);

        predNode = node;
#endif //ENABLE_CFG

        // if this is a call site, create new subgraph at this place
        if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
            Function *callFunc = CInst->getCalledFunction();

            // if we don't have this subgraph constructed, construct it
            // else just add call edge
            if (constructedFunctions.count(callFunc) == 0) {
                LLVMDependenceGraph *subgraph = new LLVMDependenceGraph();
                subgraph->build(callFunc);

                // make the new graph a subgraph of current node
                node->addSubgraph(subgraph);
            } else {
                node->addSubgraph(constructedFunctions[callFunc]);
            }
        }
    }

    return true;
}

// workqueue element
struct WE {
    WE(llvm::BasicBlock *b, llvm::BasicBlock *p):BB(b), pred(p) {}

    llvm::BasicBlock *BB;
    llvm::BasicBlock *pred;
};

bool LLVMDependenceGraph::build(llvm::Function *func)
{
    using namespace llvm;

    // create entry node
    LLVMDGNode *entry = new LLVMDGNode(func);
    addNode(entry);
    setEntry(entry);

    constructedFunctions.insert(make_pair(func, this));

    std::queue<struct WE *> WQ;

    BasicBlock *BB = &func->getEntryBlock();
    WQ.push(new WE(BB, NULL));

    while (!WQ.empty()) {
        struct WE *item = WQ.front();
        WQ.pop();

        build(item->BB, item->pred);

        for (auto S = succ_begin(BB), SE = succ_end(BB); S != SE; ++S) {
            WQ.push(new WE(*S, BB));
        }

        delete item;
    }

    // add CFG edge from entry point to the first instruction
    entry->addSucc((*this)[(func->getEntryBlock().begin())]);

    // add top-level def-use chains
    // iterate over all nodes and for each node add data dependency
    // to its uses in llvm
    for (auto I = begin(), E = end(); I != E; ++I) {
        const llvm::Value *val = I->first;

        for (auto U = val->user_begin(), EU = val->user_end(); U != EU; ++U) {
            const llvm::Value *use = *U;

            if (val == use)
                continue;

            //errs() << *val << " USE " << *use << "\n";
            LLVMDGNode *nu = operator[](use);
            if (!nu)
                continue;
            //assert((nu || isa<Function>(use)) && "Node not constructed for use");

            I->second->addDataDependence(nu);
        }
    }

}

} // namespace dg

#endif /* HAVE_LLVM */
