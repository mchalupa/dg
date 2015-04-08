/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#ifndef ENABLE_POSTDOM
 #error "Need post-dom enabled for building LLVM Dependence Graph"
#endif

#include <utility>
#include <queue>
#include <set>

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

    if (refcount == 0) {
        delete this;
        return 0;
    }

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

bool
LLVMDependenceGraph::buildSubgraph(LLVMDGNode *node)
{
    using namespace llvm;

    const Value *val = node->getValue();
    const CallInst *CInst = dyn_cast<CallInst>(val);

    assert(CInst && "buildSubgraph called on non-CallInst");

    Function *callFunc = CInst->getCalledFunction();

    // if we don't have this subgraph constructed, construct it
    // else just add call edge
    LLVMDependenceGraph *&subgraph = constructedFunctions[callFunc];

    if (!subgraph) {
        // since we have reference the the pointer in
        // constructedFunctions, we can assing to it
        subgraph = new LLVMDependenceGraph();
        bool ret = subgraph->build(callFunc);

        // at least for now use just assert, if we'll
        // have a reason to handle such failures at some
        // point, we can change it
        assert(ret && "Building subgraph failed");
    }

    // make the subgraph a subgraph of current node
    node->addSubgraph(subgraph);
    node->addActualParameters(subgraph);
}

static LLVMDependenceGraph::LLVMDGBasicBlock *
createBasicBlock(LLVMDGNode *firstNode,
                 LLVMDependenceGraph::LLVMDGBasicBlock *predBB)
{
    // uhh, it would kill me to write it all the time
    typedef LLVMDependenceGraph::LLVMDGBasicBlock BasicBlock;

    // XXX we're leaking basic block right now
    BasicBlock *nodesBB = new BasicBlock(firstNode);

    // if we have predcessor block, we can create edges
    // if we do not have predcessor node, this is probably
    // entry node. If it is not, it is a bug, but should be
    // handled in caller
    if (predBB)
        predBB->addSuccessor(nodesBB);

    return nodesBB;
}

static bool
is_func_defined(const llvm::CallInst *CInst)
{
    llvm::Function *callFunc = CInst->getCalledFunction();

    if (callFunc->size() == 0) {
#if DEBUG_ENABLED
        llvm::errs() << "Skipping undefined function '"
                     << callFunc->getName() << "'\n";
#endif
        return false;
    }

    return true;
}

bool LLVMDependenceGraph::build(llvm::BasicBlock *BB,
                                llvm::BasicBlock *pred)
{
    using namespace llvm;

    BasicBlock::const_iterator IT = BB->begin();
    const llvm::Value *val = &(*IT);

    LLVMDGNode *node = NULL;
    LLVMDGNode *predNode = NULL;
    LLVMDGBasicBlock *nodesBB;
    LLVMDGBasicBlock *predBB = NULL;

    // get a predcessor basic block if it exists
    if (pred) {
        LLVMDGNode *pn = getNode(pred->getTerminator());
        assert(pn && "Predcessor node is not created");

        predBB = pn->getBasicBlock();
        assert(predBB && "No basic block in predcessor node");
    }

    node = new LLVMDGNode(val);
    addNode(node);

    nodesBB = createBasicBlock(node, predBB);

    // if we don't have predcessor, this is the entry BB
    if (predBB == NULL)
        setEntryBB(nodesBB);

    if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
        if (is_func_defined(CInst))
            buildSubgraph(node);
    }

    ++IT; // shift to next instruction, we have the first one handled
    predNode = node;

    for (BasicBlock::const_iterator Inst = IT, EInst = BB->end();
         Inst != EInst; ++Inst) {

        val = &(*Inst);
        node = new LLVMDGNode(val);
        // add new node to this dependence graph
        addNode(node);
        node->setBasicBlock(nodesBB);

        // add successor to predcessor node
        if (predNode)
            predNode->addSuccessor(node);

        // set new predcessor node
        predNode = node;

        // if this is a call site, create new subgraph at this place
        if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
            if (is_func_defined(CInst))
                buildSubgraph(node);
        }
    }

    // check if this is the exit node of function
    TerminatorInst *term = BB->getTerminator();
    if (!term) {
        errs() << "Basic block is not well formed\n" << *BB << "\n";
        return false;
    }

    // create one unified exit node from function and add control dependence
    // to it from every return instruction. We could use llvm pass that
    // would do it for us, but then we would lost the advantage of working
    // on dep. graph that is not for whole llvm
    const ReturnInst *ret = dyn_cast<ReturnInst>(term);
    if (ret) {
        LLVMDGNode *ext = getExit();
        if (!ext) {
            // we need new llvm value, so that the nodes won't collide
            ReturnInst *phonyRet
                = ReturnInst::Create(ret->getContext(), BB);
            if (!phonyRet) {
                errs() << "Failed creating phony return value "
                       << "for exit node\n";
                return false;
            }

            phonyRet->setName("EXIT");

            ext = new LLVMDGNode(phonyRet);
            addNode(ext);
            setExit(ext);

            LLVMDGBasicBlock *retBB = new LLVMDGBasicBlock(ext, ext);
            setExitBB(retBB);
        }

        // add control dependence from this (return) node
        // to EXIT node
        assert(node && "BUG, no node after we went through basic block");
        node->addControlDependence(ext);
        nodesBB->addSuccessor(getExitBB());
    }

    // set last node
    nodesBB->setLastNode(node);

    // sanity check if we have the first and the last node set
    assert(nodesBB->getFirstNode() && "No first node in BB");
    assert(nodesBB->getLastNode() && "No last node in BB");

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

    assert(func && "Passed no func");

    // do we have anything to process?
    if (func->size() == 0)
        return false;

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
                errs() << *S;
                ni = find(S->begin());
                pi = find(item->BB->getTerminator());

#ifdef DEBUG_ENABLED
                if(ni == end()) {
                    errs() << "No node for " << *(S->begin()) << "\n";
                    abort();
                }

                if(pi == end()) {
                    errs() << "No node for "
                           << *(item->BB->getTerminator()) << "\n";
                    abort();
                }
#else
                assert(ni != end());
                assert(pi != end());
#endif // DEBUG_ENABLED

                // add basic block edges
                LLVMDGBasicBlock *BB = pi->second->getBasicBlock();
                assert(BB && "Do not have BB");

                LLVMDGBasicBlock *succBB = ni->second->getBasicBlock();
                assert(succBB && "Do not have predcessor BB");

                BB->addSuccessor(succBB);
                continue;
            }

            WQ.push(new WE(*S, item->BB));
        }

        delete item;
    }

    // add CFG edge from entry point to the first instruction
    entry->addSuccessor(getNode(func->getEntryBlock().begin()));

    addFormalParameters();

    addPostDomTree();

    addTopLevelDefUse();
    addIndirectDefUse();

    // check if we have everything
    assert(getEntry() && "Missing entry node");
    assert(getExit() && "Missing exit node");
    assert(getEntryBB() && "Missing entry BB");
    assert(getExitBB() && "Missing exit BB");

    return true;
}

void LLVMDependenceGraph::addTopLevelDefUse()
{
    // add top-level def-use chains
    // iterate over all nodes and for each node add data dependency
    // to its uses in llvm
    for (auto I = begin(), E = end(); I != E; ++I) {
        const llvm::Value *val = I->first;

        assert(val && "key is NULL in dg::nodes");

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
    assert(CInst && "addActualParameters called on non-CallInst");

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
    assert(func && "entry node value is not a function");

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

void LLVMDependenceGraph::addPostDomTree()
{
    std::queue<LLVMDGBasicBlock *> queue;
    unsigned int run_id;

    LLVMDGBasicBlock *exitBB = getExitBB();
    assert(exitBB && "Tried creating post-dom tree without BBs");

    run_id = exitBB->getDFSRun();
    exitBB->setDFSRun(++run_id);
    queue.push(exitBB);

    while (!queue.empty()) {
        LLVMDGBasicBlock *BB = queue.front();
        queue.pop();
        BB->setDFSRun(run_id);

        for (LLVMDGBasicBlock *predBB : BB->predcessors()) {
            if (predBB->successorsNum() == 1) {
                // BB immediately post-dominates the predBB
                BB->addIPostDom(predBB);
            }

            if (predBB->getDFSRun() != run_id)
                queue.push(predBB);
        }
    }
}

} // namespace dg

#endif /* HAVE_LLVM */
