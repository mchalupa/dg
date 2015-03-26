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

/// ------------------------------------------------------------------
//  -- LLVMDGNode
/// ------------------------------------------------------------------
LLVMDependenceGraph *LLVMDGNode::addSubgraph(LLVMDependenceGraph *sub)
{
    // increase references
    sub->ref();

    // call parent's addSubgraph
    return DGNode<LLVMDependenceGraph, LLVMDGNode *>::addSubgraph(sub);
}

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    for (auto I = begin(), E = end(); I != E; ++I) {
        if (I->second) {
            LLVMDependenceGraph *subgraph = I->second->getSubgraph();
            if (subgraph)
                // graphs are referenced, once the refcount is 0
                // the graph will be deleted
                subgraph->unref();

            if (I->second->getParameters()) {
                int rc = I->second->getParameters()->unref();
                assert(rc == 0 && "parameters had more than one reference");
            }

            delete I->second;
        }
    }
}

int LLVMDependenceGraph::unref()
{
    --refcount;

    if (refcount == 0)
        delete this;

    return refcount;
}

inline bool LLVMDependenceGraph::addNode(LLVMDGNode *n)
{
    return DependenceGraph<const llvm::Value *, LLVMDGNode *>
                                        ::addNode(n->getValue(), n);
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
            LLVMDependenceGraph *&subgraph = constructedFunctions[callFunc];
            if (!subgraph) {
                // since we have reference the the pointer in
                // constructedFunctions, we can assing to it
                subgraph = new LLVMDependenceGraph();
                subgraph->build(callFunc);

                // make the new graph a subgraph of current node
                node->addSubgraph(subgraph);
                node->addActualParameters(subgraph);
            } else {
                node->addSubgraph(subgraph);
                node->addActualParameters(subgraph);
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

#if DEBUG_ENABLED
    llvm::errs() << "Building graph for '" << func->getName() << "'\n";
#endif

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

    addFormalParameters();
    addTopLevelDefUse();
    addIndirectDefUse();
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

void LLVMDependenceGraph::addIndirectDefUse()
{
}

void LLVMDGNode::addActualParameters(LLVMDependenceGraph *funcGraph)
{
    using namespace llvm;

    const CallInst *CInst = dyn_cast<CallInst>(value);
    const Function *func = CInst->getCalledFunction();

    // do not add redundant nodes
    if (func->arg_size() == 0)
        return;

    LLVMDependenceGraph *params = new LLVMDependenceGraph();
    LLVMDependenceGraph *old = addParameters(params);
    assert(old == NULL && "Replaced parameters");

    // create entry node for params
    LLVMDGNode *en = new LLVMDGNode(value);
    params->addNode(en);
    params->setEntry(en);

    for (auto I = func->arg_begin(), E = func->arg_end();
         I != E; ++I) {
        const Value *val = (&*I);

        LLVMDGNode *nn = new LLVMDGNode(val);
        params->addNode(nn);

        // add control edges
        en->addControlDependence(nn);

        // add parameter edges -- these are just normal dependece edges
        LLVMDGNode *fp = (*funcGraph)[val];
        assert(fp && "Do not have formal parametr");
        nn->addDataDependence(fp);
    }

    return;
}

void LLVMDependenceGraph::addFormalParameters()
{
    using namespace llvm;

    LLVMDGNode *entryNode = getEntry();
    assert(entryNode);

    const Function *func = dyn_cast<Function>(entryNode->getValue());
    if (func->arg_size() == 0)
        return;

    for (auto I = func->arg_begin(), E = func->arg_end();
         I != E; ++I) {
        const Value *val = (&*I);

        LLVMDGNode *nn = new LLVMDGNode(val);
        addNode(nn);

        // add control edges
        bool ret = entryNode->addControlDependence(nn);
        assert(ret && "Already have formal parameters");
    }
}

} // namespace dg

#endif /* HAVE_LLVM */
