/// XXX add licence
//

#ifndef HAVE_LLVM
# error "Need LLVM for LLVMDependenceGraph"
#endif

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#include <utility>
#include <unordered_map>
#include <set>
#include <vector>

#include "analysis/BFS.h"

#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>

#include "Utils.h"
#include "LLVMDGVerifier.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

#include "PointsTo.h"
#include "DefUse.h"

#include "llvm-debug.h"

using llvm::errs;
using std::make_pair;

namespace dg {

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------

// map of all constructed functions
std::map<const llvm::Value *, LLVMDependenceGraph *> constructedFunctions;

const std::map<const llvm::Value *, LLVMDependenceGraph *>& getConstructedFunctions()
{
    return constructedFunctions;
}

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    // delete nodes
    for (auto I = begin(), E = end(); I != E; ++I) {
        LLVMNode *node = I->second;

        if (node) {
            for (auto subgraph : node->getSubgraphs()) {
                // graphs are referenced, once the refcount is 0
                // the graph will be deleted
                // Because of recursive calls, graph can be its
                // own subgraph. In that case we're in the destructor
                // already, so do not delete it
                    subgraph->unref(subgraph != this);
            }

            LLVMDGParameters *params = node->getParameters();
            if (params) {
                for (auto par : *params) {
                    delete par.second.in;
                    delete par.second.out;
                }

                delete params;
            }

            if (!node->getBBlock()
                && !llvm::isa<llvm::Function>(*I->first))
                DBG("WARN: Value " << *I->first << "had no BB assigned");

            delete node;
        } else {
            DBG("WARN: Value " << *I->first << "had no node assigned");
        }
    }

    // delete post-dominator tree root
    delete getPostDominatorTreeRoot();
}

static void addGlobals(llvm::Module *m, LLVMDependenceGraph *dg)
{
    for (auto I = m->global_begin(), E = m->global_end(); I != E; ++I)
        dg->addGlobalNode(new LLVMNode(&*I));
}

bool LLVMDependenceGraph::verify() const
{
    LLVMDGVerifier verifier(this);
    return verifier.verify();
}

bool LLVMDependenceGraph::build(llvm::Module *m, const llvm::Function *entry)
{
    // get entry function if not given
    if (!entry)
        entry = m->getFunction("main");

    if (!entry) {
        errs() << "No entry function found/given\n";
        return false;
    }

    module = m;

    // add global nodes. These will be shared across subgraphs
    addGlobals(m, this);

    // build recursively DG from entry point
    build(entry);

    return true;
};

LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node)
{
    using namespace llvm;

    const Value *val = node->getValue();
    const CallInst *CInst = dyn_cast<CallInst>(val);
    assert(CInst && "buildSubgraph called on non-CallInst");
    const Function *callFunc = CInst->getCalledFunction();

    return buildSubgraph(node, callFunc);
}

// FIXME don't duplicate the code
bool LLVMDependenceGraph::addFormalGlobal(const llvm::Value *val)
{
    // add the same formal parameters
    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    // if we have this value, just return
    if (params->find(val))
        return false;

    LLVMNode *fpin = new LLVMNode(val);
    LLVMNode *fpout = new LLVMNode(val);
    params->addGlobal(val, fpin, fpout);

    LLVMNode *entry = getEntry();
    entry->addControlDependence(fpin);
    entry->addControlDependence(fpout);

    return true;
}

static bool addSubgraphGlobParams(LLVMDependenceGraph *parentdg,
                                  LLVMDGParameters *params)
{
    bool changed = false;
    for (auto it = params->global_begin(), et = params->global_end();
         it != et; ++it)
        changed |= parentdg->addFormalGlobal(it->first);

    // and add heap-allocated variables
    for (auto it : *params) {
        if (llvm::isa<llvm::CallInst>(it.first))
            changed |= parentdg->addFormalParameter(it.first);
    }

    return changed;
}

void LLVMDependenceGraph::addSubgraphGlobalParameters(LLVMDependenceGraph *subgraph)
{
    LLVMDGParameters *params = subgraph->getParameters();
    if (!params)
        return;

    // if we do not change anything, it means that the graph
    // has these params already, and so must the callers of the graph
    if (!addSubgraphGlobParams(this, params))
        return;

    // recursively add the formal parameters to all callers
    for (LLVMNode *callsite : this->getCallers()) {
        LLVMDependenceGraph *graph = callsite->getDG();
        graph->addSubgraphGlobalParameters(this);

        // update the actual parameters of the call-site
        callsite->addActualParameters(this);
    }
}

LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node, const llvm::Function *callFunc)
{
    using namespace llvm;

    LLVMBBlock *BB;

    // if we don't have this subgraph constructed, construct it
    // else just add call edge
    LLVMDependenceGraph *&subgraph = constructedFunctions[callFunc];
    if (!subgraph) {
        // since we have reference the the pointer in
        // constructedFunctions, we can assing to it
        subgraph = new LLVMDependenceGraph();
        // set global nodes to this one, so that
        // we'll share them
        subgraph->setGlobalNodes(getGlobalNodes());
        subgraph->module = module;
        // make subgraphs gather the call-sites too
        subgraph->gatherCallsites(gather_callsites, gatheredCallsites);

        // make the real work
        bool ret = subgraph->build(callFunc);

        // at least for now use just assert, if we'll
        // have a reason to handle such failures at some
        // point, we can change it
        assert(ret && "Building subgraph failed");

        // we built the subgraph, so it has refcount = 1,
        // later in the code we call addSubgraph, which
        // increases the refcount to 2, but we need this
        // subgraph to has refcount 1, so unref it
        subgraph->unref(false /* deleteOnZero */);
    }

    BB = node->getBBlock();
    assert(BB && "do not have BB; this is a bug, sir");
    BB->addCallsite(node);

    // add control dependence from call node
    // to entry node
    node->addControlDependence(subgraph->getEntry());

    // add globals that are used in subgraphs
    // it is necessary if this subgraph was creating due to function
    // pointer call
    addSubgraphGlobalParameters(subgraph);
    node->addActualParameters(subgraph, callFunc);

    return subgraph;
}

static bool
is_func_defined(const llvm::CallInst *CInst)
{
    llvm::Function *callFunc = CInst->getCalledFunction();

    if (!callFunc || callFunc->size() == 0)
        return false;

    return true;
}

bool LLVMDependenceGraph::addFormalParameter(const llvm::Value *val)
{
    // add the same formal parameters
    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    // if we have this value, just return
    if (params->find(val))
        return false;

    LLVMNode *fpin = new LLVMNode(val);
    LLVMNode *fpout = new LLVMNode(val);
    params->add(val, fpin, fpout);

    LLVMNode *entry = getEntry();
    entry->addControlDependence(fpin);
    entry->addControlDependence(fpout);

    return true;
}

// FIXME copied from PointsTo.cpp, don't duplicate,
// add it to analysis generic
static bool isMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return false;

    const llvm::StringRef& name = func->getName();
    if (name.equals("malloc") || name.equals("calloc") || name.equals("realloc"))
        return true;

    return false;
}

void LLVMDependenceGraph::handleInstruction(const llvm::Value *val,
                                            LLVMNode *node)
{
    using namespace llvm;

    if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
        const Value *strippedValue = CInst->getCalledValue()->stripPointerCasts();
        const Function *func = dyn_cast<Function>(strippedValue);
        // if func is nullptr, then this is indirect call
        // via function pointer. We cannot do something with
        // that here, we don't know the points-to
        if (func && gather_callsites &&
            strcmp(func->getName().data(), gather_callsites) == 0) {
            gatheredCallsites->insert(node);
        }

        if (is_func_defined(CInst)) {
            LLVMDependenceGraph *subg = buildSubgraph(node);
            node->addSubgraph(subg);
        }

        // if we allocate a memory in a function, we can pass
        // it to other functions, so it is like global.
        // We need it as parameter, so that if we define it,
        // we can add def-use edges from parent, through the parameter
        // to the definition
        if (isMemAllocationFunc(CInst->getCalledFunction()))
                addFormalParameter(val);

        // no matter what is the function, this is a CallInst, so create call-graph
        addCallNode(node);
    } else if (const Instruction *Inst = dyn_cast<Instruction>(val)) {
        if (isa<LoadInst>(val) || isa<GetElementPtrInst>(val)) {
            const Value *op = Inst->getOperand(0)->stripInBoundsOffsets();
             if (isa<GlobalVariable>(op))
                 addFormalGlobal(op);
        } else if (isa<StoreInst>(val)) {
            const Value *op = Inst->getOperand(0)->stripInBoundsOffsets();
            if (isa<GlobalVariable>(op))
                addFormalGlobal(op);

            op = Inst->getOperand(1)->stripInBoundsOffsets();
            if (isa<GlobalVariable>(op))
                addFormalGlobal(op);
        }
    }
}

LLVMBBlock *LLVMDependenceGraph::build(const llvm::BasicBlock& llvmBB)
{
    using namespace llvm;

    BasicBlock::const_iterator IT = llvmBB.begin();
    const Value *val = &(*IT);

    LLVMNode *predNode = nullptr;
    LLVMNode *node = new LLVMNode(val);
    LLVMBBlock *BB = new LLVMBBlock(node);
    BB->setKey(&llvmBB);

    addNode(node);
    handleInstruction(val, node);

    ++IT; // shift to next instruction, we have the first one handled
    predNode = node;

    // iterate over the instruction and create node for every single
    // one of them + add CFG edges
    for (BasicBlock::const_iterator Inst = IT, EInst = llvmBB.end();
         Inst != EInst; ++Inst) {

        val = &(*Inst);
        node = new LLVMNode(val);
        // add new node to this dependence graph
        addNode(node);

        // add successor to predcessor node
        if (predNode)
            predNode->setSuccessor(node);

        // set new predcessor node for next iteration
        predNode = node;

        // take instruction specific actions
        handleInstruction(val, node);
    }

    // check if this is the exit node of function
    const TerminatorInst *term = llvmBB.getTerminator();
    if (!term) {
        DBG("WARN: Basic block is not well formed\n" << llvmBB);
        return BB;
    }

    // create one unified exit node from function and add control dependence
    // to it from every return instruction. We could use llvm pass that
    // would do it for us, but then we would lost the advantage of working
    // on dep. graph that is not for whole llvm
    const ReturnInst *ret = dyn_cast<ReturnInst>(term);
    if (ret) {
        LLVMNode *ext = getExit();
        if (!ext) {
            // we need new llvm value, so that the nodes won't collide
            ReturnInst *phonyRet
                = ReturnInst::Create(ret->getContext()/*, ret->getReturnValue()*/);
            if (!phonyRet) {
                errs() << "ERR: Failed creating phony return value "
                       << "for exit node\n";
                // XXX later we could return somehow more mercifully
                abort();
            }

            ext = new LLVMNode(phonyRet);
            addNode(ext);
            setExit(ext);

            LLVMBBlock *retBB = new LLVMBBlock(ext, ext);
            setExitBB(retBB);
        }

        // add control dependence from this (return) node
        // to EXIT node
        assert(node && "BUG, no node after we went through basic block");
        node->addControlDependence(ext);
        BB->addSuccessor(getExitBB());
    }

    // set last node
    BB->setLastNode(node);

    // sanity check if we have the first and the last node set
    assert(BB->getFirstNode() && "No first node in BB");
    assert(BB->getLastNode() && "No last node in BB");

    return BB;
}

bool LLVMDependenceGraph::build(const llvm::Function *func)
{
    using namespace llvm;

    assert(func && "Passed no func");

    // do we have anything to process?
    if (func->size() == 0)
        return false;

    // create entry node
    LLVMNode *entry = new LLVMNode(func);
    addGlobalNode(entry);
    setEntry(entry);

    constructedFunctions.insert(make_pair(func, this));
    constructedBlocks.reserve(func->size());

    // add formal parameters to this graph
    addFormalParameters();

    // iterate over basic blocks
    for (const llvm::BasicBlock& llvmBB : *func) {
        LLVMBBlock *BB = build(llvmBB);
        constructedBlocks[&llvmBB] = BB;

        // first basic block is the entry BB
        if (!getEntryBB())
            setEntryBB(BB);
    }

    // add CFG edges
    for (auto it : constructedBlocks) {
        const BasicBlock *llvmBB = it.first;
        LLVMBBlock *BB = it.second;

        for (succ_const_iterator S = succ_begin(llvmBB), SE = succ_end(llvmBB);
             S != SE; ++S) {
            LLVMBBlock *succ = constructedBlocks[*S];
            assert(succ && "Missing basic block");

            BB->addSuccessor(succ);
        }
    }

    // check if we have everything
    assert(getEntry() && "Missing entry node");
    assert(getExit() && "Missing exit node");
    assert(getEntryBB() && "Missing entry BB");
    assert(getExitBB() && "Missing exit BB");

    // add CFG edge from entry point to the first instruction
    entry->addControlDependence(getEntryBB()->getFirstNode());

    return true;
}

void LLVMDependenceGraph::addFormalParameters()
{
    using namespace llvm;

    LLVMNode *entryNode = getEntry();
    assert(entryNode && "No entry node when adding formal parameters");

    const Function *func = dyn_cast<Function>(entryNode->getValue());
    assert(func && "entry node value is not a function");
    //assert(func->arg_size() != 0 && "This function is undefined?");
    if (func->arg_size() == 0)
        return;

    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    LLVMNode *in, *out;
    for (auto I = func->arg_begin(), E = func->arg_end();
         I != E; ++I) {
        const Value *val = (&*I);

        in = new LLVMNode(val);
        out = new LLVMNode(val);
        params->add(val, in, out);

        // add control edges
        entryNode->addControlDependence(in);
        entryNode->addControlDependence(out);
    }
}

static void computePDFrontiers(LLVMBBlock *BB)
{
    for (LLVMBBlock *pred : BB->predcessors()) {
        LLVMBBlock *ipdom = BB->getIPostDom();
        if (ipdom && ipdom != BB)
            BB->addPostDomFrontier(pred);
    }

    for (LLVMBBlock *pdom : BB->getPostDominators()) {
        for (LLVMBBlock *df : pdom->getPostDomFrontiers()) {
            LLVMBBlock *ipdom = df->getIPostDom();
            if (ipdom && ipdom != BB)
                BB->addPostDomFrontier(df);
        }
    }
}

static void queuePostDomBBs(LLVMBBlock *BB, std::vector<LLVMBBlock *> *blocks)
{
    blocks->push_back(BB);
}

static void computePostDominanceFrontiers(LLVMBBlock *root)
{
    std::vector<LLVMBBlock *> blocks;
    analysis::BBlockBFS<LLVMNode> bfs(analysis::BFS_BB_POSTDOM);

    // get BBs in the order of post-dom tree edges
    bfs.run(root, queuePostDomBBs, &blocks);

    // go bottom-up the post-dom tree and compute post-domninance frontiers
    for (int i = blocks.size() - 1; i >= 0; --i)
        computePDFrontiers(blocks[i]);
}

void LLVMDependenceGraph::computePostDominators(bool addPostDomFrontiers)
{
    using namespace llvm;
    PostDominatorTree *pdtree = new PostDominatorTree();

    // iterate over all functions
    for (auto F : constructedFunctions) {
        // root of post-dominator tree
        LLVMBBlock *root = nullptr;
        Value *val = const_cast<Value *>(F.first);
        Function& f = *cast<Function>(val);
        // compute post-dominator tree for this function
        pdtree->runOnFunction(f);

        // add immediate post-dominator edges
        auto our_blocks = F.second->getConstructedBlocks();
        bool built = false;
        for (auto it : our_blocks) {
            LLVMBBlock *BB = it.second;
            BasicBlock *B = const_cast<BasicBlock *>(it.first);
            DomTreeNode *N = pdtree->getNode(B);
            // sometimes there's not even single node for the post-dominator tree, try:
            // https://raw.githubusercontent.com/dbeyer/sv-benchmarks/master/c/bitvector/jain_4_true-unreach-call.i
            if (!N)
                continue;

            DomTreeNode *idom = N->getIDom();
            BasicBlock *idomBB = idom ? idom->getBlock() : nullptr;
            built = true;

            if (idomBB) {
                LLVMBBlock *pb = our_blocks[idomBB];
                assert(pb && "Do not have constructed BB");
                BB->setIPostDom(pb);
                assert(cast<BasicBlock>(BB->getKey())->getParent()
                        == cast<BasicBlock>(pb->getKey())->getParent()
                        && "BBs are from diferent functions");
            // if we do not have idomBB, then the idomBB is a root BB
            } else {
                // PostDominatorTree may has special root without BB set
                // or it is the node without immediate post-dominator
                if (!root) {
                    root = new LLVMBBlock();
                    root->setKey(nullptr);
                    F.second->setPostDominatorTreeRoot(root);
                }

                BB->setIPostDom(root);
            }
        }

        // well, if we haven't built the pdtree, this is probably infinite loop
        // that has no pdtree. Until we have anything better, just add sound
        // edges that are not so precise - to predecessors.
        if (!built) {
            for (auto it : our_blocks) {
                LLVMBBlock *BB = it.second;
                for (LLVMBBlock *succBB : BB->successors())
                    succBB->addPostDomFrontier(BB);
            }
        }

        if (addPostDomFrontiers) {
            // assert(root && "BUG: must have root");
            if (root)
                computePostDominanceFrontiers(root);
        }
    }

    delete pdtree;
}

static bool array_match(llvm::StringRef name, const char *names[])
{
    unsigned idx = 0;
    while(names[idx]) {
        if (name.equals(names[idx]))
            return true;
        ++idx;
    }

    return false;
}

static bool match_callsite_name(LLVMNode *callNode, const char *names[])
{
    using namespace llvm;

    // if the function is undefined, it has no subgraphs,
    // but is not called via function pointer
    if (!callNode->hasSubgraphs()) {
        const CallInst *callInst = cast<CallInst>(callNode->getValue());
        const Value *calledValue = callInst->getCalledValue();
        const Function *func = dyn_cast<Function>(calledValue->stripPointerCasts());
        // in the case we haven't run points-to analysis
        if (!func)
            return false;

        return array_match(func->getName(), names);
    } else {
        // simply iterate over the subgraphs, get the entry node
        // and check it
        for (LLVMDependenceGraph *dg : callNode->getSubgraphs()) {
            LLVMNode *entry = dg->getEntry();
            assert(entry && "No entry node in graph");

            const Function *func = cast<Function>(entry->getValue()->stripPointerCasts());
            return array_match(func->getName(), names);
        }
    }

    return false;
}

bool LLVMDependenceGraph::getCallSites(const char *name, std::set<LLVMNode *> *callsites)
{
    const char *names[] = {name, NULL};
    return getCallSites(names, callsites);
}

bool LLVMDependenceGraph::getCallSites(const char *names[],
                                       std::set<LLVMNode *> *callsites)
{
    for (auto F : constructedFunctions) {
        for (auto I : F.second->constructedBlocks) {
            LLVMBBlock *BB = I.second;
            LLVMNode *n = BB->getFirstNode();
            while (n) {
                if (llvm::isa<llvm::CallInst>(n->getValue())) {
                    if (match_callsite_name(n, names))
                        callsites->insert(n);
                }

                n = n->getSuccessor();
            }
        }
    }

    return callsites->size() != 0;
}

} // namespace dg
