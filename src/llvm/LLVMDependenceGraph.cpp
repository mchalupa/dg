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
#include <ctime>

#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

#include "PointsTo.h"
#include "DefUse.h"

using llvm::errs;
using std::make_pair;


namespace dg {

namespace debug {
class TimeMeasure
{
    struct timespec s, e, r;
    clockid_t clockid;

public:
    TimeMeasure(clockid_t clkid = CLOCK_MONOTONIC)
        : clockid(clkid) {}

    void start() {
        clock_gettime(clockid, &s);
    };

    void stop() {
        clock_gettime(clockid, &e);
    };

    const struct timespec& duration()
    {
        r.tv_sec = e.tv_sec - s.tv_sec;
        if (e.tv_nsec > s.tv_nsec)
            r.tv_nsec = e.tv_nsec - s.tv_nsec;
        else {
            --r.tv_sec;
            r.tv_nsec = 1000000000 - e.tv_nsec;
        }

        return r;
    }

    void report(const char *prefix = nullptr)
    {
        // compute the duration
        duration();

        if (prefix)
            errs() << prefix << " ";

        errs() << r.tv_sec << " sec "
               << r.tv_nsec / 1000000 << "ms\n";
    }
};
} // namespace debug


/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------

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

            if (!node->getBasicBlock()
                && !llvm::isa<llvm::Function>(*I->first))
                errs() << "WARN: Value " << *I->first << "had no BB assigned\n";

            delete node;
        } else {
            errs() << "WARN: Value " << *I->first << "had no node assigned\n";
        }
    }
}

static void addGlobals(llvm::Module *m, LLVMDependenceGraph *dg)
{
    for (const llvm::GlobalVariable& gl : m->globals())
        dg->addGlobalNode(new LLVMNode(&gl));
}

bool LLVMDependenceGraph::build(llvm::Module *m, llvm::Function *entry)
{
    debug::TimeMeasure tm;
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

    analysis::LLVMPointsToAnalysis PTA(this);

    tm.start();
    PTA.run();
    tm.stop();
    tm.report("INFO: Points-to analysis took");

    analysis::LLVMDefUseAnalysis DUA(this);

    tm.start();
    DUA.run();  // compute reaching definitions
    tm.stop();
    tm.report("INFO: Reaching defs analysis took");

    tm.start();
    DUA.addDefUseEdges(); // add def-use edges according that
    tm.stop();
    tm.report("INFO: Adding Def-Use edges took");

    return true;
};

bool LLVMDependenceGraph::buildSubgraph(LLVMNode *node)
{
    using namespace llvm;

    LLVMBBlock *BB;
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
        // set global nodes to this one, so that
        // we'll share them
        subgraph->setGlobalNodes(getGlobalNodes());
        subgraph->module = module;
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

    BB = node->getBasicBlock();
    assert(BB && "do not have BB; this is a bug, sir");
    BB->addCallsite(node);

    // make the subgraph a subgraph of current node
    // addSubgraph will increase refcount of the graph
    node->addSubgraph(subgraph);
    node->addActualParameters(subgraph);

    return true;
}

static bool
is_func_defined(const llvm::CallInst *CInst)
{
    llvm::Function *callFunc = CInst->getCalledFunction();

    if (!callFunc || callFunc->size() == 0)
        return false;

    return true;
}


void LLVMDependenceGraph::handleInstruction(const llvm::Value *val,
                                            LLVMNode *node)
{
    using namespace llvm;

    if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
        if (gather_callsites &&
            strcmp(CInst->getCalledFunction()->getName().data(),
                   gather_callsites) == 0)
            gatheredCallsites.insert(node);

        if (is_func_defined(CInst))
            buildSubgraph(node);
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
        errs() << "WARN: Basic block is not well formed\n" << llvmBB << "\n";
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
    LLVMNode *entry = new LLVMNode(func);
    addNode(entry);
    setEntry(entry);

    constructedFunctions.insert(make_pair(func, this));
    std::unordered_map<llvm::BasicBlock *, LLVMBBlock *> createdBlocks;
    createdBlocks.reserve(func->size());

    // add formal parameters to this graph
    addFormalParameters();

    // iterate over basic blocks
    for (llvm::BasicBlock& llvmBB : *func) {
        LLVMBBlock *BB = build(llvmBB);
        createdBlocks[&llvmBB] = BB;

        // first basic block is the entry BB
        if (!getEntryBB())
            setEntryBB(BB);
    }

    // add CFG edges
    for (auto it : createdBlocks) {
        BasicBlock *llvmBB = it.first;
        LLVMBBlock *BB = it.second;
        bool add_cd = llvmBB->getTerminator()->getNumSuccessors() > 1;

        for (succ_iterator S = succ_begin(llvmBB), SE = succ_end(llvmBB);
             S != SE; ++S) {
            LLVMBBlock *succ = createdBlocks[*S];
            assert(succ && "Missing basic block");

            BB->addSuccessor(succ);

            // if this basic block is terminated with predicate,
            // then the successors are control dependent on it
            if (add_cd)
                BB->addControlDependence(succ);
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

    LLVMDGParameters *params = new LLVMDGParameters();
    setParameters(params);

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

} // namespace dg
