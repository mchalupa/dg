#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"
#include "DefUse.h"
#include "AnalysisGeneric.h"

#include "analysis/DFS.h"
#include "llvm-debug.h"

using namespace llvm;

/// --------------------------------------------------
//   Add def-use edges
/// --------------------------------------------------
namespace dg {
namespace analysis {

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg)
{
    Module *m = dg->getModule();
    // set data layout
    DL = m->getDataLayout();
}

Pointer LLVMDefUseAnalysis::getConstantExprPointer(const ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

// FIXME don't duplicate the code from DefUse.cpp
static DefMap *getDefMap(LLVMNode *n)
{
    DefMap *r = n->getData<DefMap>();
    if (!r) {
        r = new DefMap();
        n->setData(r);
    }

    // must always have
    assert(r);

    return r;
}

LLVMNode *LLVMDefUseAnalysis::getOperand(LLVMNode *node,
                                        const Value *val, unsigned int idx)
{
    return dg::analysis::getOperand(node, val, idx, DL);
}

static void addIndirectDefUsePtr(const Pointer& ptr, LLVMNode *to, DefMap *df)
{
    if (!ptr.isKnown()) {
        DBG("ERR: pointer pointing to unknown location, UNSOUND! "
               << *to->getKey());
        return;
    }

    LLVMNode *ptrnode = ptr.obj->node;
    const Value *ptrVal = ptrnode->getKey();
    // functions does not have indirect reaching definitions
    if (isa<Function>(ptrVal))
        return;

    ValuesSetT& defs = df->get(ptr);
    // do we have any reaching definition at all?
    if (defs.empty()) {
        // we do not add initial def to global variables because not all
        // global variables could be used in the code and we'd redundantly
        // iterate through the defintions. Do it lazily here.
        if (isa<GlobalVariable>(ptrVal)) {
                // ok, so the GV was defined in initialization phase,
                // so the reaching definition for the ptr is there.
                // If it was not defined, then we still want the edge
                // from the global node in this case
                defs.insert(ptrnode);
        } else if (isa<AllocaInst>(ptrVal)) {
            // AllocaInst without any reaching definition
            // may mean that the value is undefined. Nevertheless
            // we use the value that is defined via the AllocaInst,
            // so add definition on the AllocaInst
            // This is the same as with global variables
            defs.insert(ptrnode);
        } else if (isa<ConstantPointerNull>(ptrVal)) {
            // just do nothing, it has no reaching definition
            return;
        } else {
            DBG("WARN: no reaching definition for " << *ptr.obj->node->getKey()
                << " + " << *ptr.offset);
            return;
        }
    }

    assert(!defs.empty());
    for (LLVMNode *n : defs)
        n->addDataDependence(to);

    // if we got pointer to our object with UNKNOWN_OFFSET,
    // it still can be reaching definition, so we must take it into
    // account
    ValuesSetT& defsUnknown = df->get(Pointer(ptr.obj, UNKNOWN_OFFSET));
    if (!defsUnknown.empty()) {
        for (LLVMNode *n : defsUnknown)
            n->addDataDependence(to);
    }
}

static void addIndirectDefUse(LLVMNode *ptrNode, LLVMNode *to, DefMap *df)
{
    // iterate over all memory locations that this
    // store can define and check where they are defined
    for (const Pointer& ptr : ptrNode->getPointsTo())
        addIndirectDefUsePtr(ptr, to, df);
}

// return Value used on operand LLVMNode
// It is either the operand itself or
// global value used in ConstantExpr if the
// operand is ConstantExpr
static void addStoreLoadInstDefUse(LLVMNode *storeNode, LLVMNode *op, DefMap *df)
{
    const Value *val = op->getKey();
    if (isa<ConstantExpr>(val)) {
        // it should be one ptr
        PointsToSetT& PS = op->getPointsTo();
        assert(PS.size() == 1);

        const Pointer& ptr = *PS.begin();
        addIndirectDefUsePtr(ptr, storeNode,  df);
    } else {
        op->addDataDependence(storeNode);
    }
}

void LLVMDefUseAnalysis::handleStoreInst(const StoreInst *Inst, LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *valNode = node->getOperand(1);

    // this node uses what is defined on valNode
    if (valNode) {
        addStoreLoadInstDefUse(node, valNode, df);
    }
#ifdef DEBUG_ENABLED
    else {
        if (!isa<ConstantInt>(Inst->getValueOperand()))
            DBG("ERR def-use: Unhandled value operand for " << *Inst);
    }
#endif

    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode);

    // and also uses what is defined on ptrNode
    addStoreLoadInstDefUse(node, ptrNode, df);
}

void addDefUseToUnknownLocation(LLVMNode *node, DefMap *df)
{
    ValuesSetT& S = df->get(UnknownMemoryLocation);

    for (LLVMNode *n : S)
        n->addDataDependence(node);
}

void LLVMDefUseAnalysis::handleLoadInst(const llvm::LoadInst *Inst,
                                        LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *ptrNode = getOperand(node, Inst->getPointerOperand(), 0);
    assert(ptrNode && "No ptr node");

    // load inst is reading from the memory,
    // so add indirect def-use edges
    addIndirectDefUse(ptrNode, node, df);

    // load inst is reading from the memory that is pointed by
    // the top-level value, so add def-use edge
    addStoreLoadInstDefUse(node, ptrNode, df);

    // if we have any reaching definition (write to memory)
    // to unknown location, this may be load from it.
    // We must add def-use edges, to keep it sound
    addDefUseToUnknownLocation(node, df);
}

static void addOutParamsEdges(LLVMDGParameter& p, DefMap *df)
{
    // points to set is contained in the input param
    for (const Pointer& ptr : p.in->getPointsTo()) {
        for (auto it : *df) {
            // get all the pointers that have the same object
            if (it.first.obj == ptr.obj) {
                // ok, the memory location is defined in this subgraph,
                // so add data dependence edge to the out param
                for (LLVMNode *def : it.second)
                    def->addDataDependence(p.out);
            }
        }
    }
}

static void addOutParamsEdges(LLVMDependenceGraph *graph)
{
    LLVMNode *exitNode = graph->getExit();
    assert(exitNode && "No exit node in subgraph");
    DefMap *df = getDefMap(exitNode);

    // add edges between formal params and the output params
    LLVMDGParameters *params = graph->getParameters();
    if (params) {
        // add edges to parameters
        for (auto it : *params) {
            const Value *val = it.first;
            if (!val->getType()->isPointerTy())
                continue;

            addOutParamsEdges(it.second, df);
        }

        // add edges to parameter globals
        for (auto I = params->global_begin(), E = params->global_end();
             I != E; ++I) {
            addOutParamsEdges(I->second, df);
        }
    }
}

static void addReturnEdge(LLVMNode *callNode, LLVMDependenceGraph *subgraph)
{
    // FIXME we're loosing some accuracy here and
    // this edges causes that we'll go into subprocedure
    // even with summary edges
    if (!callNode->isVoidTy())
        subgraph->getExit()->addDataDependence(callNode);
}

static void addOutParamsEdges(LLVMNode *callNode)
{
    for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs()) {
        addOutParamsEdges(subgraph);
        addReturnEdge(callNode, subgraph);
    }
}

static void addDefUseToOperands(LLVMNode *node,
                                LLVMDGParameters *params, DefMap *df)
{
    for (int i = 1, e = node->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = node->getOperand(i);
        if (!op)
            continue;

        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
            DBG("ERR: no actual param for " << *op->getKey());
            continue;
        }

        if (op->isPointerTy()) {
            // add data dependencies to in parameters
            addIndirectDefUse(op, p->in, df);
            // fall-through to
            // add also top-level def-use edge
        }

        op->addDataDependence(p->in);
    }
}

static void addDefUseToParameterGlobals(LLVMNode *node,
                                        LLVMDGParameters *params, DefMap *df)
{
    LLVMDependenceGraph *dg = node->getDG();
    for (auto I = params->global_begin(), E = params->global_end(); I != E; ++I) {
        LLVMDGParameter& p = I->second;
        // we add the def-use edges to input parameters
        LLVMNode *g = dg->getNode(I->first);
        if (!g) {
            errs() << "ERR: no global param: " << *I->first << "\n";
            continue;
        }

        if (g->isPointerTy()) {
            // add data dependencies to in parameters
            addIndirectDefUse(g, p.in, df);
            // fall-through to
            // add also top-level def-use edge
        }

        g->addDataDependence(p.in);
    }
}

static void handleUndefinedCall(LLVMNode *node)
{
    const CallInst *CI = cast<CallInst>(node->getKey());
    // the function is undefined - add the top-level deps
    LLVMDependenceGraph *dg = node->getDG();
    for (auto I = CI->op_begin(), E = CI->op_end(); I != E; ++I) {
        const Value *op = *I;
        LLVMNode *from;

        if (isa<ConstantExpr>(op))
            from = dg->getNode(op->stripPointerCasts());
        else
            from = dg->getNode(op);

        if (from)
            from->addDataDependence(node);
    }
}

static void handleCallInst(LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    const CallInst *CI = cast<CallInst>(node->getKey());
    const Function *func = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts());

    // if this is call via function pointer, add the
    // data dependence edge to corresponding node
    if (!func) {
        LLVMNode *n = node->getOperand(0);
        if (n)
            n->addDataDependence(node);
    }

    if (func && func->size() == 0) {
        handleUndefinedCall(node);
        return;
    }

    // add edges from last definition in the subgraph to
    // output parameters. Must be here, because here
    // we add even return edge (does not depend on params)
    addOutParamsEdges(node);

    // have we something to do further?
    LLVMDGParameters *params = node->getParameters();
    if (!params)
        return;

    // add def-use edges between parameters and the operands
    // parameters begin from 1
    addDefUseToOperands(node, params, df);

    // add def-use edges to parameter globals
    addDefUseToParameterGlobals(node, params, df);
}

static void handleInstruction(const Instruction *Inst, LLVMNode *node)
{
    LLVMDependenceGraph *dg = node->getDG();

    for (auto I = Inst->op_begin(), E = Inst->op_end(); I != E; ++I) {
        LLVMNode *op = dg->getNode(*I);
        if (op)
            op->addDataDependence(node);
/* this is hit with switch
#ifdef DEBUG_ENABLED
        else if (!isa<ConstantInt>(*I) && !isa<BranchInst>(Inst))
            DBG("WARN: no node for operand " << **I
                   << "in " << *Inst);
#endif
*/
    }
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node)
{
    const Value *val = node->getKey();

    if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        handleStoreInst(Inst, node);
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        handleLoadInst(Inst, node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    } else if (const Instruction *Inst = dyn_cast<Instruction>(val)) {
        handleInstruction(Inst, node); // handle rest of Insts
    } else {
        DBG("ERR: Unhandled instruction " << *val);
    }

    // we will run only once
    return false;
}

} // namespace analysis
} // namespace dg
