#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "llvm/LLVMNode.h"
#include "llvm/LLVMDependenceGraph.h"
#include "llvm-debug.h"

#include "AnalysisGeneric.h"
#include "DefUse.h"

using namespace llvm;

/// --------------------------------------------------
//   Add def-use edges
/// --------------------------------------------------
namespace dg {
namespace analysis {
namespace old {

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg), DL(new DataLayout(dg->getModule()))
{
}

Pointer LLVMDefUseAnalysis::getConstantExprPointer(ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

static void handleInstruction(const Instruction *Inst, LLVMNode *node)
{
    LLVMDependenceGraph *dg = node->getDG();

    for (auto I = Inst->op_begin(), E = Inst->op_end(); I != E; ++I) {
        LLVMNode *op = dg->getNode(*I);
        if (op)
            op->addDataDependence(node);
    }
}

static void addReturnEdge(LLVMNode *callNode, LLVMDependenceGraph *subgraph)
{
    // FIXME we may loose some accuracy here and
    // this edges causes that we'll go into subprocedure
    // even with summary edges
    if (!callNode->isVoidTy())
        subgraph->getExit()->addDataDependence(callNode);
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
                                        Value *val, unsigned int idx)
{
    return dg::analysis::getOperand(node, val, idx, DL);
}

static bool getParamsForPointer(LLVMDGParameters *params,
                                const Pointer& ptr, std::set<LLVMNode *>& nodes)
{
    const Pointer ptrUnkn(ptr.obj, UNKNOWN_OFFSET);
    for (auto it : *params) {
        LLVMDGParameter& p = it.second;

        // the points-to set is in the input param
        PointsToSetT& S = p.in->getPointsTo();

        // we could get this pointer via any param that
        // points somewhere to that object
        for (const Pointer& sp : S)
            if (sp.obj == ptr.obj)
                nodes.insert(p.in);
    }

    return !nodes.empty();
}

void LLVMDefUseAnalysis::addInitialDefuse(LLVMDependenceGraph *dg,
                                          ValuesSetT& defs, const Pointer& ptr,
                                          uint64_t len)
{
    LLVMNode *ptrnode = ptr.obj->node;
    Value *ptrVal = ptrnode->getKey();

    assert(defs.empty() && "Adding initial def-use to something defined");

    // functions does not have indirect reaching definitions
    if (isa<Function>(ptrVal))
        return;

    std::set<LLVMNode *> nodes;
    LLVMDGParameters *params = dg->getParameters();

    if (params && getParamsForPointer(params, ptr, nodes)) {
        // we found a parameter that uses this pointer? Then the initial
        // edge should go there and we need to add the def-use edge
        // from def to this parameter in caller, since this is use of the value
        // that is defined in the caller
        for (LLVMNode *n : nodes)
            defs.insert(n);

        for (LLVMNode *callsite : dg->getCallers()) {
            DefMap *csdf = getDefMap(callsite);
            for (LLVMNode *n : nodes) {
                addIndirectDefUsePtr(ptr, *n->rev_data_begin(), csdf, len);
            }
        }
    } else if (isa<GlobalVariable>(ptrVal)) {
        // we do not add initial def to global variables because not all
        // global variables could be used in the code and we'd redundantly
        // iterate through the defintions. Do it lazily here.

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
    }
}

static uint64_t getAffectedMemoryLength(const Value *val, const DataLayout *DL)
{
    uint64_t size = 0;
    Type *elemTy;

    // if the value passed is store inst, then use
    // the value operand
    if (const Instruction *I = dyn_cast<Instruction>(val)) {
        if (isa<StoreInst>(val))
            val = I->getOperand(0);
    }

    Type *Ty = val->getType();
    if (Ty->isPointerTy())
        elemTy = Ty->getContainedType(0);
    else
        elemTy = Ty;

    if (elemTy->isSized())
        size = DL->getTypeAllocSize(elemTy);
#ifdef DEBUG_ENABLED
    else if (!elemTy->isFunctionTy())
        errs() << "ERR def-use: type pointed is not sized " << *elemTy << "\n";
#endif

    return size;
}

static uint64_t getAffectedMemoryLength(LLVMNode *node, const DataLayout *DL)
{
    return getAffectedMemoryLength(node->getValue(), DL);
}

static bool isDefinitionInRange(uint64_t off, uint64_t len,
                                const Pointer& dptr, ValuesSetT& defs,
                                const DataLayout *DL)
{
    uint64_t doff = *dptr.offset;
    if (doff == off)
        return true;

    if (doff < off) {
        // check if definition, that has lesser offset than the
        // offset in our pointer can write to our memory
        for (LLVMNode *n : defs) {
            Value *v = n->getValue();
            // the only instruction that can write into memory is store inst
            // (and actually some intrinsic insts, but we don't care about those here)
            if (!isa<StoreInst>(v))
                continue;

            uint64_t len = getAffectedMemoryLength(n->getOperand(0), DL);
            if (doff + len >= off)
                return true;
        }
    } else {
        // else check the offset of definition pointer
        // is inside the range [off, off + len]
        // (len is the number of bytes we're reading from memory)
        return dptr.offset.inRange(off, off + len);
    }

    return false;
}

// @param len - how many bytes from offset in pointer are we reading (using)
void LLVMDefUseAnalysis::addIndirectDefUsePtr(const Pointer& ptr, LLVMNode *to,
                                              DefMap *df, uint64_t len)
{
    if (!ptr.isKnown()) {
        if (!ptr.isNull())
            DBG("ERR: pointer pointing to unknown location, UNSOUND! "
                << *to->getKey());
        return;
    }

    // get all pointers with the very same object as ptr
    std::pair<DefMap::iterator,
              DefMap::iterator> objects = df->getObjectRange(ptr);

    // iterate over the pointers and check if we have reaching
    // definitions for the offsets that are in the center of
    // interest - that is for offsets that could affect this
    // particular use of pointer
    bool found = false;
    for (auto it = objects.first; it != objects.second; ++it) {
        const Pointer& dptr = it->first;
        ValuesSetT& defs = it->second;

        // if the offset of the pointer is unknown,
        // then any definition is relevant for us
        if (ptr.offset.isUnknown()) {
            for (LLVMNode *n : defs)
                n->addDataDependence(to);

            continue;
        }

        if (isDefinitionInRange(*ptr.offset, len, dptr, defs, DL)) {
            found = true;
            for (LLVMNode *n : defs)
                n->addDataDependence(to);
        }
    }

    // if we have no definition for our pointer,
    // try to add initial def-use edges
    ValuesSetT& defs = df->get(ptr);
    if (!found && defs.empty()) {
        addInitialDefuse(to->getDG(), defs, ptr, len);
        for (LLVMNode *n : defs) {
            n->addDataDependence(to);
        }
    }
}

void LLVMDefUseAnalysis::addIndirectDefUse(LLVMNode *ptrNode, LLVMNode *to,
                                           DefMap *df)
{
    uint64_t len = getAffectedMemoryLength(ptrNode, DL);
    for (const Pointer& ptr : ptrNode->getPointsTo()) {
        addIndirectDefUsePtr(ptr, to, df, len);

        // if we got pointer to our object with UNKNOWN_OFFSET,
        // it still can be reaching definition, so we must take it into
        // account
        ValuesSetT& defsUnknown = df->get(Pointer(ptr.obj, UNKNOWN_OFFSET));
        if (!defsUnknown.empty()) {
            for (LLVMNode *n : defsUnknown)
                n->addDataDependence(to);
        }
    }
}

// return Value used on operand LLVMNode
// It is either the operand itself or
// global value used in ConstantExpr if the
// operand is ConstantExpr
void LLVMDefUseAnalysis::addStoreLoadInstDefUse(LLVMNode *storeNode,
                                                LLVMNode *op, DefMap *df)
{
    Value *val = op->getKey();
    if (isa<ConstantExpr>(val)) {
        // it should be one ptr
        PointsToSetT& PS = op->getPointsTo();
        assert(PS.size() == 1 && "ConstantExpr with more pointers");

        const Pointer& ptr = *PS.begin();
        uint64_t len = getAffectedMemoryLength(storeNode, DL);
        addIndirectDefUsePtr(ptr, storeNode, df, len);
    } else
        op->addDataDependence(storeNode);
}

void LLVMDefUseAnalysis::handleStoreInst(StoreInst *Inst, LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    LLVMNode *valNode = node->getOperand(1);

    // this node uses what is defined on valNode
    if (valNode) {
        addStoreLoadInstDefUse(node, valNode, df);
    }
#ifdef DEBUG_ENABLED
    else {
        Value *valOp = Inst->getValueOperand();
        if (!isa<ConstantInt>(valOp) && !isa<ConstantPointerNull>(valOp))
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

void LLVMDefUseAnalysis::handleLoadInst(llvm::LoadInst *Inst,
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

static void addOutParamsEdges(const Pointer& ptr, LLVMNode *to, DefMap *df)
{
    auto bounds = df->getObjectRange(ptr);
    for (auto it = bounds.first; it != bounds.second; ++it) {
        // ok, the memory location is defined in this subgraph,
        // so add data dependence edge to the out param
        for (LLVMNode *def : it->second)
            def->addDataDependence(to);
    }
}

static void addOutParamsEdges(LLVMDGParameter& p, DefMap *df)
{
    // points to set is contained in the input param
    for (const Pointer& ptr : p.in->getPointsTo()) {
        addOutParamsEdges(ptr, p.out, df);

        // check if a memory location of the pointer is defined
        if (!ptr.isKnown())
            continue;

        for (auto memit : ptr.obj->pointsTo)
            for (const Pointer& memptr : memit.second)
                addOutParamsEdges(memptr, p.out, df);
    }
}

static void addOutParamsEdges(LLVMDependenceGraph *graph)
{
    LLVMNode *exitNode = graph->getExit();
    // this function has no exit node - that means
    // that it ends with unreachable (or invoke, which is
    // not handled yet), so there's nothing we can do here.
    // Nothing will get out of this function
    if (!exitNode)
        return;

    DefMap *df = getDefMap(exitNode);

    // add edges between formal params and the output params
    LLVMDGParameters *params = graph->getParameters();
    if (params) {
        // add edges to parameters
        for (auto it : *params) {
            Value *val = it.first;
            if (!val->getType()->isPointerTy())
                continue;

            addOutParamsEdges(it.second, df);
        }

        // add edges to parameter globals
        for (auto I = params->global_begin(), E = params->global_end();
             I != E; ++I) {
            addOutParamsEdges(I->second, df);
        }

        LLVMDGParameter *vaparam = params->getVarArg();
        if (vaparam)
            addOutParamsEdges(*vaparam, df);
    }
}

static void addOutParamsEdges(LLVMNode *callNode)
{
    for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs()) {
        addOutParamsEdges(subgraph);
        addReturnEdge(callNode, subgraph);
    }
}

void LLVMDefUseAnalysis::addDefUseToParamNode(LLVMNode *op, DefMap *df, LLVMNode *to)
{
    if (op->isPointerTy()) {
        // add data dependencies to in parameters
        // this adds def-use edges between definition
        // of the pointer and the parameter
        addIndirectDefUse(op, to, df);

        // and check if the memory the pointer points to has some reaching
        // definition
        for (const Pointer& ptr : op->getPointsTo()) {
            if (!ptr.isKnown()) {
                if (!ptr.isNull())
                    errs() << "ERR: unknown pointer, may be unsound\n";
                continue;
            }

            for (auto it : ptr.obj->pointsTo) {
                for (const Pointer& memptr : it.second) {
                    // XXX is it 0 always?
                    addIndirectDefUsePtr(memptr, to, df, 0);
                }
            }
        }
        // fall-through to
        // add also top-level def-use edge
    }

    op->addDataDependence(to);
}

void LLVMDefUseAnalysis::addDefUseToParam(LLVMNode *op, DefMap *df,
                                          LLVMDGParameter *p)
{
    addDefUseToParamNode(op, df, p->in);
}

void LLVMDefUseAnalysis::addDefUseToOperands(LLVMNode *node,
                                             bool isvararg,
                                             LLVMDGParameters *params,
                                             DefMap *df)
{
    for (int i = 1, e = node->getOperandsNum(); i < e; ++i) {
        LLVMNode *op = node->getOperand(i);
        if (!op)
            continue;

        LLVMDGParameter *p = params->find(op->getKey());
        if (!p) {
            if (isvararg) {
                // we don't have actual vararg, but it doesn matter since
                // we'd added all to one arg. Just add the def-use to call-site
                addDefUseToParamNode(op, df, node);
            } else
                DBG("ERR: no actual param for " << *op->getKey());

            continue;
        }

        addDefUseToParam(op, df, p);
    }
}

void LLVMDefUseAnalysis::addDefUseToParameterGlobals(LLVMNode *node,
                                                     LLVMDGParameters *params,
                                                     DefMap *df)
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

void LLVMDefUseAnalysis::handleUndefinedCall(LLVMNode *node, CallInst *CI)
{
    // the function is undefined - add the top-level deps
    LLVMDependenceGraph *dg = node->getDG();
    for (auto I = CI->op_begin(), E = CI->op_end(); I != E; ++I) {
        Value *op = *I;
        LLVMNode *from;

        if (isa<ConstantExpr>(op))
            from = dg->getNode(op->stripPointerCasts());
        else
            from = dg->getNode(op);

        if (from)
            from->addDataDependence(node);
    }
}

void LLVMDefUseAnalysis::handleIntrinsicCall(LLVMNode *callNode, CallInst *CI)
{
    IntrinsicInst *I = cast<IntrinsicInst>(CI);
    Value *dest, *src = nullptr;
    DefMap *df = getDefMap(callNode);

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            dest = I->getOperand(0);
            src = I->getOperand(1);
            break;
        case Intrinsic::memset:
            dest = I->getOperand(0);
            break;
        default:
            handleUndefinedCall(callNode, CI);
            return;
    }

    // we must have dest set
    assert(dest);

    LLVMNode *destNode = getOperand(callNode, dest, 1);
    assert(destNode && "No dest operand for intrinsic call");

    LLVMNode *srcNode = nullptr;
    if (src) {
        srcNode = getOperand(callNode, src, 2);
        assert(srcNode && "No src operand for intrinsic call");
    }

    // these functions touch the memory of the pointers
    addIndirectDefUse(destNode, callNode, df);
    if (srcNode)
        addIndirectDefUse(srcNode, callNode, df);

    // and we also need the top-level edges. These will be added by
    // handle undefined call
    handleUndefinedCall(callNode, CI);
}

void LLVMDefUseAnalysis::handleUndefinedCall(LLVMNode *node)
{
    CallInst *CI = cast<CallInst>(node->getKey());
    Function *func
        = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts());
    if (func && func->isIntrinsic())
        handleIntrinsicCall(node, CI);
    else
        handleUndefinedCall(node, CI);
}

void LLVMDefUseAnalysis::handleInlineAsm(LLVMNode *callNode)
{
    CallInst *CI = cast<CallInst>(callNode->getValue());
    LLVMDependenceGraph *dg = callNode->getDG();

    // the last operand is the asm itself, so iterate only to e - 1
    for (unsigned i = 0, e = CI->getNumOperands(); i < e - 1; ++i) {
        Value *opVal = CI->getOperand(i);
        if (!opVal->getType()->isPointerTy())
            continue;

        LLVMNode *opNode = dg->getNode(opVal->stripInBoundsOffsets());
        if (!opNode) {
            // it may be constant expression
            opNode = getOperand(callNode, opVal, i);
            assert((!opNode || (opNode->getKey() == opVal)) && "got wrong operand");
        }

        assert(opNode && "Do not have an operand for inline asm");

        // if nothing else, this call at least uses the operands
        opNode->addDataDependence(callNode);
    }
}

void LLVMDefUseAnalysis::handleCallInst(LLVMNode *node)
{
    DefMap *df = getDefMap(node);
    CallInst *CI = cast<CallInst>(node->getKey());

    if (CI->isInlineAsm()) {
        handleInlineAsm(node);
        return;
    }

    Function *func
        = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts());

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
    bool va = func ? func->isVarArg() : false;
    addDefUseToOperands(node, va, params, df);

    // add def-use edges to parameter globals
    addDefUseToParameterGlobals(node, params, df);
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node, LLVMNode *prev)
{
    Value *val = node->getKey();
    (void) prev;

    if (StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        handleStoreInst(Inst, node);
    } else if (LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        handleLoadInst(Inst, node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    } else if (Instruction *Inst = dyn_cast<Instruction>(val)) {
        handleInstruction(Inst, node); // handle rest of Insts
    } else {
        DBG("ERR: Unhandled instruction " << *val);
    }

    // we will run only once
    return false;
}

} // namespace old
} // namespace analysis
} // namespace dg
