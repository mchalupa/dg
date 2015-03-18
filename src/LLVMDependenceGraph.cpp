/// XXX add licence
//

#ifdef HAVE_LLVM

#include <utility>
#include <queue>
#include <set>

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

    std::set<llvm::BasicBlock *> processedBB;
    std::queue<struct WE *> WQ;

    WQ.push(new WE(&func->getEntryBlock(), NULL));

    while (!WQ.empty()) {
        struct WE *item = WQ.front();
        WQ.pop();

        build(item->BB, item->pred);

        int i = 0;
        for (auto S = succ_begin(item->BB), SE = succ_end(item->BB);
             S != SE; ++S) {

            // when program contain loops, it is possible that
            // we added this block to queue more times.
            // In this case just create the CFG edge, but do not
            // process this node any further. It would lead to
            // infinite loop
            iterator ni, pi;
            if (!processedBB.insert(*S).second) {
#if ENABLE_CFG
                errs() << *S;
                ni = find(S->begin());
                pi = find(item->BB->getTerminator());
                assert(ni != end());
                assert(pi != end());

#if DEBUG_ENABLED
                // set loop Header instruction
                const Instruction *hinst
                    = dyn_cast<Instruction>(ni->second->getValue());
                assert(hinst && "BUG");
                const Value *header = hinst->getParent()->getTerminator();
                (*this)[header]->setIsLoopHeader();
#endif

                pi->second->addSucc(ni->second);
#endif
                continue;
            }

            WQ.push(new WE(*S, item->BB));
        }

        delete item;
    }

    // add CFG edge from entry point to the first instruction
    entry->addSucc((*this)[(func->getEntryBlock().begin())]);

    addTopLevelDefUse();
}

void LLVMDependenceGraph::addTopLevelDefUse()
{
    // add top-level def-use chains
    // iterate over all nodes and for each node add data dependency
    // to its uses in llvm
    for (auto I = begin(), E = end(); I != E; ++I) {
        const llvm::Value *val = I->first;

        for (auto U = val->user_begin(), EU = val->user_end(); U != EU; ++U) {
            const llvm::Value *use = *U;

            if (val == use)
                continue;

            LLVMDGNode *nu = operator[](use);
            if (!nu)
                continue;

            I->second->addDataDependence(nu);
        }
    }
}

} // namespace dg

#endif /* HAVE_LLVM */
