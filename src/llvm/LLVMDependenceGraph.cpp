/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#include <utility>
#include <unordered_map>
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

LLVMNode **LLVMNode::findOperands()
{
    using namespace llvm;
    const Value *val = getKey();

    if (const AllocaInst *Inst = dyn_cast<AllocaInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(val);
        operands_num = 1;
    } else if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        operands = new LLVMNode *[2];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands[1] = dg->getNode(Inst->getValueOperand());
        operands_num = 2;
        assert(operands[0] && "StoreInst pointer operand without node");
        if (!operands[1]) {
            errs() << "WARN: StoreInst value operand without node: "
                   << *Inst->getValueOperand() << "\n";
        }
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands_num = 1;
    } else if (const GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands_num = 1;
    } else if (const CallInst *Inst = dyn_cast<CallInst>(val)) {
        // we store the called function as a first operand
        // and all the arguments as the other operands
        operands_num = Inst->getNumArgOperands() + 1;
        operands = new LLVMNode *[operands_num];
        operands[0] = dg->getNode(Inst->getCalledValue());
        for (int i = 0; i < operands_num - 1; ++i)
            operands[i + 1] = dg->getNode(Inst->getArgOperand(i));
    } else if (const ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getReturnValue());
        operands_num = 1;
    } else if (const CastInst *Inst = dyn_cast<CastInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->stripPointerCasts());
        if (!operands[0])
            errs() << "WARN: CastInst with unstrippable pointer cast" << *Inst << "\n";
        operands_num = 1;
    }
}

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
LLVMDependenceGraph::buildSubgraph(LLVMNode *node)
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


void LLVMDependenceGraph::handleInstruction(const llvm::Value *val, LLVMNode *node)
{
    using namespace llvm;

    if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
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

        for (succ_iterator S = succ_begin(llvmBB), SE = succ_end(llvmBB); S != SE; ++S) {
            LLVMBBlock *succ = createdBlocks[*S];
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

    addFormalParameters();

    return true;
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph)
{
    using namespace llvm;

    const CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

    // do not add redundant nodes
    const Function *func = CInst->getCalledFunction();
    if (func->arg_size() == 0)
        return;

    LLVMDGParameters *params = new LLVMDGParameters();
    LLVMDGParameters *old = addParameters(params);
    assert(old == nullptr && "Replaced parameters");

    LLVMNode *in, *out;
    for (const Value *val : CInst->arg_operands()) {
        in = new LLVMNode(val);
        out = new LLVMNode(val);
        params->add(val, in, out);

        // add control edges from this node to parameters
        addControlDependence(in);
        addControlDependence(out);

        // add parameter edges -- these are just normal dependece edges
        //LLVMNode *fp = (*funcGraph)[val];
        //assert(fp && "Do not have formal parametr");
        //nn->addDataDependence(fp);
    }
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
    entryNode->addParameters(params);

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

#endif /* HAVE_LLVM */
