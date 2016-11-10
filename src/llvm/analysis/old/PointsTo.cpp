#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm-debug.h"

#include "PointsTo.h"
#include "AnalysisGeneric.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMPointsToAnalysis::LLVMPointsToAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg), DL(new DataLayout(dg->getModule()))
{
    handleGlobals();
}

static bool handleMemAllocation(LLVMNode *node, size_t size = 0,
                                bool isheap = false)
{
    // every global is a pointer
    MemoryObj *&mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(node, size, isheap);
        node->addPointsTo(mo);
        return true;
    }

    return false;
}

bool LLVMPointsToAnalysis::handleAllocaInst(LLVMNode *node)
{
    return handleMemAllocation(node);
}

static bool handleGlobal(LLVMNode *node)
{
    // DON'T - every global is a pointer, even when the type
    // it has is a non-pointer
    //if (!Inst->getType()->isPointerTy())
    //    return false;

    return handleMemAllocation(node);
}

LLVMNode *LLVMPointsToAnalysis::getOperand(LLVMNode *node,
                                           Value *val, unsigned int idx)
{
    return dg::analysis::getOperand(node, val, idx, DL);
}

static bool handleStoreInstPtr(LLVMNode *valNode, LLVMNode *ptrNode)
{
    bool changed = false;

    // iterate over points to locations of pointer node
    // FIXME what if the memory location is undefined?
    // in that case it has no points-to set and we're
    // loosing information
    for (auto ptr : ptrNode->getPointsTo()) {
        // if we're storing a pointer, make obj[offset]
        // points to the same locations as the valNode
        for (auto valptr : valNode->getPointsTo())
            changed |= ptr.obj->addPointsTo(ptr.offset, valptr);
    }

    return changed;
}

bool LLVMPointsToAnalysis::handleStoreInst(StoreInst *Inst, LLVMNode *node)
{
    // get ptrNode before checking if value type is pointer type,
    // because the pointer operand can be ConstantExpr and in getOperand()
    // we resolve its points-to set
    LLVMNode *ptrNode = getOperand(node, Inst->getPointerOperand(), 0);

    Value *valOp = Inst->getValueOperand();
    if (!valOp->getType()->isPointerTy())
        return false;

    LLVMNode *valNode = getOperand(node, valOp, 1);
    assert(ptrNode && "No ptr node");
    assert(valNode && "No val node");

    return handleStoreInstPtr(valNode, ptrNode);
}

Pointer LLVMPointsToAnalysis::getConstantExprPointer(ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

static void removeConcreteOffsets(LLVMNode *node, const Pointer& ptr)
{
    PointsToSetT& S = node->getPointsTo();
    // remove pointers to the same memory but with concrete offset
    // -> we don't need them, that is all covered in UNKNOWN_OFFSET
    for (PointsToSetT::iterator I = S.begin(); I != S.end(); ) {
        if ((I->obj == ptr.obj) && !I->offset.isUnknown()) {
            PointsToSetT::iterator tmp = I++;
            S.erase(tmp);
        } else
            ++I;
    }
}

static bool addPtrWithUnknownOffset(LLVMNode *node, const Pointer& ptr)
{
    bool ret = node->addPointsTo(ptr.obj, UNKNOWN_OFFSET);
    if (ret)
        removeConcreteOffsets(node, ptr);

    return ret;
}

static uint64_t getMemSize(MemoryObj *mo, const Type *ptrTy, const DataLayout *DL)
{
    if (mo->size != 0)
        return mo->size;

    const Value *ptrVal = mo->node->getKey();
    Type *Ty = ptrVal->getType()->getContainedType(0);

    // Type can be i8 *null or similar
    if (!Ty->isSized() || isa<ConstantPointerNull>(ptrVal)) {
        Ty = ptrTy->getContainedType(0);
        if (!Ty->isSized())
            return 0;
    }

    return DL->getTypeAllocSize(Ty);
}

bool LLVMPointsToAnalysis::handleLoadInstPtr(const Pointer& ptr, LLVMNode *node)
{
    bool changed = false;

    // load of pointer makes this node point
    // to the same values as ptrNode
    if (!ptr.isKnown()) {
        // load from (possible) nullptr does not change anything for us
        // XXX actually, it is undefined behaviour, so shouldn't we
        // set it to unknown memory location? - no, because we don't know
        // if it really points to null, it is just a possibility
        if (!ptr.isNull())
            // if the pointer is unknown, just make it pointing to unknown
            changed |= node->addPointsTo(UnknownMemoryLocation);
    } else if (ptr.offset.isUnknown()) {
        // if we don't know where into the object
        // the pointer points to, we must add everything
        for (auto it : ptr.obj->pointsTo) {
            for (const Pointer& p : it.second)
                changed |= node->addPointsTo(p);
        }
    } else {
        uint64_t size;
        for (auto memptr : ptr.obj->pointsTo[ptr.offset]) {
            if (!memptr.isKnown()) {
              // use offset 0, so that we won't have unknown pointers with
              // all different offsets
              changed |= node->addPointsTo(Pointer(memptr.obj, 0));
              continue;
            }

            // check the size here, we can be in loop due to GEP's
            // it has no sense to add pointers with offsets greater
            // that the object we points-to
            Type *mo_ty = memptr.obj->node->getValue()->getType();
            if (mo_ty->isPointerTy()) {
                size = getMemSize(memptr.obj, mo_ty, DL);
                if (size == 0) {
                    changed |= addPtrWithUnknownOffset(node, memptr);
                    continue;
                }

                if (*memptr.offset >= size) {
                    if (!memptr.offset.isUnknown())
                    DBG("INFO: cropping LoadInst, off > size: " << *memptr.offset
                        << " " << size << "\n     in " << *node->getKey());
                    changed |= addPtrWithUnknownOffset(node, memptr);
                    continue;
                }
            }

            changed |= node->addPointsTo(memptr);
        }

        // if the memory contains a pointer on unknown offset,
        // it may be relevant for us, because it can be on the ptr.offset,
        // so we need to add it too
        if (ptr.obj->pointsTo.count(UNKNOWN_OFFSET) != 0)
            for (auto memptr : ptr.obj->pointsTo[UNKNOWN_OFFSET])
                changed |= node->addPointsTo(memptr);
    }

    return changed;
}

bool LLVMPointsToAnalysis::handleLoadInstPointsTo(LLVMNode *ptrNode,
                                                  LLVMNode *node)
{
    bool changed = false;

    // get values that are referenced by pointer and
    // store them as values of this load (or as pointsTo
    // if the load it pointer)
    for (auto ptr : ptrNode->getPointsTo())
        changed |= handleLoadInstPtr(ptr, node);

    return changed;
}

bool LLVMPointsToAnalysis::handleLoadInst(LoadInst *Inst, LLVMNode *node)
{
    if (!Inst->getType()->isPointerTy())
        return false;

    LLVMNode *ptrNode = getOperand(node, Inst->getPointerOperand(), 0);
    assert(ptrNode && "No ptr node");

    return handleLoadInstPointsTo(ptrNode, node);
}

static bool addPtrWithOffset(LLVMNode *ptrNode, LLVMNode *node,
                             uint64_t offset, const DataLayout *DL)
{
    bool changed = false;
    Offset off = offset;
    uint64_t size;

    for (auto ptr : ptrNode->getPointsTo()) {
        if (ptr.obj->isUnknown() || ptr.offset.isUnknown()) {
            // don't store unknown with different offsets
            changed |= addPtrWithUnknownOffset(node, ptr);
        } else {
            size = getMemSize(ptr.obj, ptrNode->getValue()->getType(), DL);
            if (size == 0) {
                // if the type is not size, we cannot compute the
                // offset correctly
                changed |= addPtrWithUnknownOffset(node, ptr);
                continue;
            }

            off += ptr.offset;


            // ivalid offset might mean we're cycling, like:
            //
            //  %a = alloca [5 x i32]
            //  %p = alloca i32 *
            //  S %a, %p
            //  %0 = load %p
            //  %e = getelementptr %0, 1
            //  S %e, %p
            //
            // here %p points to %a + 0 and %e points to %a + 0 + 4
            // (or other offset, depending on data layout) and
            // the last store makes %p pointing to %a + 0 and %a + 4.
            // In the next data-flow round the offset gets increased again by 4,
            // so we have 0, 4, 8, 12 ... and diverging
            // We could fix it by not adding the offset, but by storing
            // the whole offsets sequence from gep, like %p -> %a, 0, 0
            // This way we wouldn't diverge and we could just compute the
            // offset after the points-to analysis stops - but, let's
            // keep it simple now and just crop invalid offsets
            if (*off >= size) {
#ifdef DEBUG_ENABLED
                if (!off.isUnknown())
                    //errs() << "INFO points-to: cropping GEP, off > size: " << *off
                    //       << " " << size << " in " << *ptrNode->getKey() << "\n";
#endif
                changed |= addPtrWithUnknownOffset(node, ptr);
            } else
                changed |= node->addPointsTo(ptr.obj, off);
        }
    }

    return changed;
}

static inline unsigned getPointerBitwidth(const DataLayout *DL, const Value *ptr)
{
    const Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

bool LLVMPointsToAnalysis::handleGepInst(GetElementPtrInst *Inst,
                                         LLVMNode *node)
{
    bool changed = false;
    Value *ptrOp = Inst->getPointerOperand();
    unsigned bitwidth = getPointerBitwidth(DL, ptrOp);
    APInt offset(bitwidth, 0);

    LLVMNode *ptrNode = getOperand(node, ptrOp, 0);
    assert(ptrNode && "Do not have GEP ptr node");

    if (Inst->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth))
            return addPtrWithOffset(ptrNode, node, offset.getZExtValue(), DL);
        else
            DBG("WARN: GEP offset greater than " << bitwidth << "-bit");
            // fall-through to UNKNOWN_OFFSET in this case
    }

    for (auto ptr : ptrNode->getPointsTo())
        // UKNOWN_OFFSET + something is still unknown
        changed |= addPtrWithUnknownOffset(node, ptr);

    return changed;
}

// @CE is ConstantExpr initializer
// @node is global's node
bool LLVMPointsToAnalysis::addGlobalPointsTo(Constant *C,
                                             LLVMNode *node,
                                             uint64_t off)
{
    Pointer ptr(&UnknownMemoryObject, 0);
    MemoryObj *mo = node->getMemoryObj();
    assert(mo && "Global has no mo");

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
        ptr = getConstantExprPointer(CE);
    } else if (isa<ConstantPointerNull>(C)) {
        ptr.obj = &NullMemoryObject;
    } else if (isa<Function>(C)) {
        // if it is a function pointer, we probably haven't built it yet,
        // so create new node
        LLVMNode *n = new LLVMNode(C);
        MemoryObj *&mo = n->getMemoryObj();
        mo = new MemoryObj(n);

        ptr.obj = mo;
        n->addPointsTo(ptr);
    } else {
        // it is a pointer to somewhere (we check that it is a pointer
        // before calling this method), so just get where - also it must be
        // a global node, since it is constant expr.
        // NOTE: Do not use just getNode(), because it would return the
        // parameter global and it has not the points-to set yet
        LLVMNode *ptrNode = dg->getGlobalNode(C);
        assert(ptrNode && "Do not have node for  pointer initializer of global");

        PointsToSetT& S = ptrNode->getPointsTo();
        // it should have only "alloc" pointer
        assert(S.size() == 1 && "Global variable has more than one pointer");
        ptr = *S.begin();
    }

    return mo->addPointsTo(off, ptr);
}

// add subghraph BBs to data-flow analysis
// (needed if we create a graph due to the function pointer)
static void addSubgraphBBs(LLVMPointsToAnalysis *PA,
                           LLVMDependenceGraph *graph)
{
    auto blocks = graph->getBlocks();
    for (auto it : blocks)
        PA->addBB(it.second);
}

static bool propagateGlobalParametersPointsTo(LLVMDGParameters *params, LLVMDependenceGraph *dg)
{
    bool changed = false;
    for (auto I = params->global_begin(), E = params->global_end(); I != E; ++I) {
        // points-to set is in the real global
        LLVMNode *glob = dg->getGlobalNode(I->first);
        assert(glob && "ERR: No global for parameter");

        PointsToSetT& S = glob->getPointsTo();

        // the only data-dependencies the params global has are those
        // to formal parameters, so use it
        LLVMNode *p = I->second.in;
        for (auto it = p->data_begin(), et = p->data_end(); it != et; ++it) {
            changed |= (*it)->addPointsTo(S);
        }
    }

    return changed;
}

static bool propagateGlobalParametersPointsTo(LLVMNode *callNode)
{
    LLVMDependenceGraph *dg = callNode->getDG();
    LLVMDGParameters *actual = callNode->getParameters();
    assert(actual && "no actual parameters");

    return propagateGlobalParametersPointsTo(actual, dg);
}

static bool propagateNewDynMemoryParamsPointsTo(LLVMDGParameters *formal,
                                                LLVMDependenceGraph *subgraph)
{
    LLVMDGParameters *subparams = subgraph->getParameters();
    if (!subparams)
        return false;

    assert(formal);

    bool changed = false;
    for (auto it : *subparams) {
        if (!llvm::isa<llvm::CallInst>(it.first))
            continue;

        // subgraph is the newly created graph, so it keeps the
        // pointer with points-to
        LLVMNode *ptrNode = subgraph->getNode(it.first);
        assert(ptrNode && "No node for in param");

        LLVMDGParameter *ap = formal->find(it.first);
        assert(ap);
        changed |= ap->in->addPointsTo(ptrNode->getPointsTo());
    }

    return changed;
}

// go recursivaly to callers and add params points-to from the subgraph
// It is needed when we add a formal parameter (in addSubgraphGlobalParameters)
// dynamically in points-to analysis
void LLVMPointsToAnalysis::addDynamicCallersParamsPointsTo(LLVMNode *callNode,
                                                           LLVMDependenceGraph *subgraph)
{
    LLVMDependenceGraph *dg = callNode->getDG();
    LLVMDGParameters *formal = dg->getParameters();
    if (!formal)
        return;

    bool changed;
    changed = propagateNewDynMemoryParamsPointsTo(formal, subgraph);

    for (LLVMNode *callsite : dg->getCallers())
        changed |= propagateGlobalParametersPointsTo(callsite);

    // if nothing changed, that this graph must have the points-to info
    // and so must the callers of them
    if (!changed)
        return;

    // recursively add points-to in callers
    for (LLVMNode *callsite : dg->getCallers())
        addDynamicCallersParamsPointsTo(callsite, dg);
}

static void propagateGlobalPointsToMain(LLVMDependenceGraph *dg);

static bool isCallInstCompatible(const Function *func, const CallInst *CI)
{
    int i = 0;
    if (func->getReturnType() != CI->getType())
        return false;

    for (auto I = func->arg_begin(), E = func->arg_end(); I != E; ++I, ++i) {
        if (!I->getType()->isPointerTy())
            continue;
        Type *CT = CI->getOperand(i)->getType();
        Type *FT = I->getType();
        if (CT != FT)
            return false;
    }

    return true;
}

bool LLVMPointsToAnalysis::handleFunctionPtrCall(LLVMNode *calledFuncNode,
                                                 LLVMNode *node)
{
    bool changed = false;
    CallInst *CI = cast<CallInst>(node->getValue());

    for (const Pointer& ptr : calledFuncNode->getPointsTo()) {
        if (!ptr.isKnown()) {
            if (!ptr.isNull())
                DBG("ERR: CallInst wrong func pointer\n");
            continue;
        }

        Function *func = dyn_cast<Function>(ptr.obj->node->getValue());
        // since we have vararg node, it is possible that the calledFuncNode
        // will point to different types, like function, alloca and whatever
        // (for exapmle the code: callva(1, func, a, "str") - this will
        // merge few different types of pointers into one node.
        // XXX maybe we could filter it in loadinst - like to consider only
        // the variables with the right type (llvmp is typed,
        // so we know that the different types are the analysis result,
        // not wrong code)
        if (!func)
            continue;

        // skip undefined functions
        if (func->size() == 0)
            continue;

        // the points-to is overapproximation, so we can have pointers that
        // actually cannot be bound to the memory location at runtime.
        // If the function arg numbuer does not match call inst nuber,
        // this is the case
        if (!func->isVarArg() && func->arg_size() != CI->getNumArgOperands())
            continue;

        if (!isCallInstCompatible(func, CI))
            continue;

        // HACK: this is a small hack. We cannot rely on
        // return value of addGlobalNode, because if this
        // function was assigned to some pointer, the global
        // node already exists and addGlobalNode will return false
        // we need to find out if we have built this function somehow
        // differently
        // NOTE: must call it before buildSubgraph, because buildSubgraph
        // will add it to constructedFunctions
        auto cf = getConstructedFunctions();
        bool isnew = cf.count(func) == 0;

        LLVMDependenceGraph *dg = node->getDG();
        LLVMDependenceGraph *subg = dg->buildSubgraph(node, func);
        LLVMNode *entry = subg->getEntry();
        dg->addGlobalNode(entry);

        // did we added this subgraph for the first time?
        if (isnew) {
            // handle new globals - there's at least on, the new entry node
            handleGlobals();
            propagatePointersToArguments(subg, CI, node);

            // add subgraph BBs now, after we propagated all
            // pointers that may be needed for it (addBB runs
            // the handlers on the BB)
            addSubgraphBBs(this, subg);
            // and this must be called now, so that the BBs are already
            // added - uff, that's crazy, we need to refactor this
            addDynamicCallersParamsPointsTo(node, subg);
            changed = true;
        }

        changed |= node->addSubgraph(subg);
    }

    return changed;
}

enum MemAllocationFuncs {
    NONEMEM = 0,
    MALLOC,
    CALLOC,
    ALLOCA,
};

static int getMemAllocationFunc(const Function *func)
{
    if (!func || !func->hasName())
        return NONEMEM;

    const char *name = func->getName().data();
    if (strcmp(name, "malloc") == 0)
        return MALLOC;
    else if (strcmp(name, "calloc") == 0)
        return CALLOC;
    else if (strcmp(name, "alloca") == 0)
        return ALLOCA;

    // realloc should overtake the memory object from former pointer

    return NONEMEM;
}

static bool handleDynamicMemAllocation(const CallInst *Inst,
                                       LLVMNode *node, int type);

static bool handleUndefinedReturnsPointer(const CallInst *Inst, LLVMNode *node)
{
    // is it call via function pointer, or it is just undefined function?
    LLVMNode *op = node->getOperand(0);
    if (op) {
        // function pointer! check if we can be malloc and similar
        for (const Pointer& ptr : op->getPointsTo()) {
            if (!ptr.isKnown()) {
                DBG("ERR: wrong pointer " << *Inst);
                continue;
            }

            const Function *func = dyn_cast<Function>(ptr.obj->node->getKey());
            // the pointer must not point to function, even when it is known,
            // because we have aggregate nodes for var arg and if this
            // pointer loads from such node, the type can be any pointer type
            if (!func)
                continue;

            if (int type = getMemAllocationFunc(func))
                return handleDynamicMemAllocation(Inst, node, type);
        }
    }

    // ok, undefined function - point to unknown memory
    return node->addPointsTo(&UnknownMemoryObject);
}

static bool handleReturnedPointer(LLVMDependenceGraph *subgraph,
                                  LLVMNode *callNode)
{
    bool changed = false;
    LLVMNode *retval = subgraph->getExit();
    // this is artificial return value, the real
    // are control dependent on it
    for (auto I = retval->rev_control_begin(), E = retval->rev_control_end();
         I != E; ++I) {
        // these can be ReturnInst or unreachable or other terminator inst
        assert(isa<TerminatorInst>((*I)->getKey()));

        for (auto ptr : (*I)->getPointsTo())
            changed |= callNode->addPointsTo(ptr);
    }

    return changed;
}

static bool propagateDynAllocationPointsTo(LLVMDependenceGraph *subgraph,
                                           LLVMDGParameters *formal)
{
    bool changed = false;
    for (auto it : *formal) {
        // only dyn. allocation parameter node can be callint,
        // becaues we have formal parameters
        if (isa<CallInst>(it.first)) {
            LLVMNode *alloc_node = subgraph->getNode(it.first);
            assert(alloc_node && "No node for dyn. mem. allocation parameter");

            changed |= it.second.in->addPointsTo(alloc_node->getPointsTo());
        }
    }

    return changed;
}

void LLVMPointsToAnalysis::propagateVarArgPointsTo(LLVMDGParameters *formal,
                                                   size_t argnum,
                                                   LLVMNode *callNode)
{
    LLVMDGParameter *vaparam = formal->getVarArg();
    assert(vaparam && "No vaarg param in vaarg function");

    size_t opnum = callNode->getOperandsNum();
    CallInst *CI = cast<CallInst>(callNode->getValue());

    for (; argnum < opnum - 1; ++argnum) {
        Value *opval = CI->getOperand(argnum);
        if (!opval->getType()->isPointerTy())
            continue;

        LLVMNode *op = getOperand(callNode, opval, argnum + 1);
        if (!op) {
            errs() << "ERR: unhandled vararg operand " << *opval << "\n";
            continue;
        }

        vaparam->in->addPointsTo(op->getPointsTo());
    }
}

bool LLVMPointsToAnalysis::
propagatePointersToArguments(LLVMDependenceGraph *subgraph,
                             const CallInst *Inst, LLVMNode *callNode)
{
    bool changed = false;
    // handle return value here, so that we can bail out
    // if we have no parameters
    if (callNode->isPointerTy())
        changed |= handleReturnedPointer(subgraph, callNode);

    LLVMDGParameters *formal = subgraph->getParameters();
    if (!formal)
        return false;

    Function *subfunc = cast<Function>(subgraph->getEntry()->getKey());

    // handle values for arguments
    // argument 0 is the called function, so start from 1
    int i = 0;
    for (auto I = subfunc->arg_begin(), E = subfunc->arg_end();
         I != E; ++I, ++i) {
        if (!I->getType()->isPointerTy())
            continue;

        LLVMDGParameter *p = formal->find(&*I);
        if (!p) {
            DBG("ERR: no such formal param: " << *I << " in " << *Inst);
            continue;
        }

        LLVMNode *op = getOperand(callNode, Inst->getArgOperand(i), i + 1);
        if (!op) {
            DBG("ERR: no operand for actual param of formal param: "
                   << *I << " in " << *Inst);
            continue;
        }

        for (const Pointer& ptr : op->getPointsTo())
            changed |= p->in->addPointsTo(ptr);
    }

    changed |= propagateDynAllocationPointsTo(subgraph, formal);
    changed |= propagateGlobalParametersPointsTo(callNode);
    if (subfunc->isVarArg())
        propagateVarArgPointsTo(formal, subfunc->arg_size(), callNode);

    return changed;
}

bool LLVMPointsToAnalysis::
propagatePointersFromArguments(LLVMDependenceGraph *subgraph, LLVMNode *callNode)
{
    LLVMDependenceGraph *calldg = callNode->getDG();
    LLVMDGParameters *formal = calldg->getParameters();
    if (!formal)
        return false;

    // FIXME the name is horrible
    return propagateNewDynMemoryParamsPointsTo(formal, subgraph);
}

static bool handleDynamicMemAllocation(const CallInst *Inst,
                                       LLVMNode *node, int type)
{
    const Value *op;
    uint64_t size = 0, size2 = 0;

    switch (type) {
        case MALLOC:
        case ALLOCA:
            op = Inst->getOperand(0);
            break;
        case CALLOC:
            op = Inst->getOperand(1);
            break;
        // FIXME: we could change the size of memory due to realloc call
        default:
            errs() << "ERR: unknown mem alloc type " << *node->getKey() << "\n";
            return false;
    };

    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
        // if the size cannot be expressed as an uint64_t,
        // just set it to 0 (that means unknown)
        if (size == ~((uint64_t) 0))
            size = 0;

        // if this is call to calloc, the size is given with
        // the first argument too
        if (type == CALLOC) {
            C = dyn_cast<ConstantInt>(Inst->getOperand(0));
            if (C) {
                size2 = C->getLimitedValue();
                if (size2 == ~((uint64_t) 0))
                    size2 = 0;
                else
                    // OK, if getting the size fails, we end up with
                    // just 1 * size - still better than 0 and UNKNOWN
                    // (it may be cropped later anyway)
                    size *= size2;
            }
        }
    }

    return handleMemAllocation(node, size, true);
}

bool LLVMPointsToAnalysis::handleMemTransfer(IntrinsicInst *I, LLVMNode *node)
{
    bool changed = false;
    Value *dest, *src, *len;

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            dest = I->getOperand(0);
            src = I->getOperand(1);
            len = I->getOperand(2);
            break;
        case Intrinsic::memset:
            errs() << "WARN: memset unhandled " << *I << "\n";
            return false;
        default:
            errs() << "ERR: unhandled mem transfer intrinsic" << *I << "\n";
            return false;
    }

    LLVMNode *destNode = getOperand(node, dest, 1);
    LLVMNode *srcNode = getOperand(node, src, 2);
    uint64_t lenval = ~((uint64_t) 0);

    if (ConstantInt *C = dyn_cast<ConstantInt>(len))
        lenval = C->getLimitedValue();

    for (const Pointer& srcptr : srcNode->getPointsTo()) {
        // set bounds from and to copy the pointers
        uint64_t from;
        uint64_t to;
        if (srcptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, make the memory be copied
            // entirely
            from = 0;
        } else {
            from = *srcptr.offset;
        }

        // FIXME: couldn't this introduce looping?
        // Just like with offsets in gep?
        if (lenval != ~((uint64_t) 0))
            // do not allow overflow
            to = from + lenval;
        else
            to = lenval;

        for (auto it : srcptr.obj->pointsTo) {
            const Offset& off = it.first;
            if (off.inRange(from, to)) {
                for (const Pointer& destptr: destNode->getPointsTo())
                    changed |= destptr.obj->addPointsTo(it.first, it.second);
            }
        }
    }

    return changed;
}

static bool handleVaStart(IntrinsicInst *I, LLVMNode *node)
{
    // vastart has only one operand which is the struct
    // it uses for storing the va arguments
    Value *vl = I->getOperand(0);
    LLVMDependenceGraph *dg = node->getDG();
    LLVMNode *valist = dg->getNode(vl);
    // this is allocainst in this function, we must have it!
    assert(valist && "Has no value for valist");

    LLVMDGParameters *params = dg->getParameters();
    // this graph must have formal parameters since
    // it is vararg (is has at least the vararg param)
    assert(params && "No formal parameters");
    LLVMDGParameter* vaparam = params->getVarArg();
    assert(vaparam && "No vararg param in vararg function");

    // the va list contains some structures that has pointers to
    // the memory, so we must create new memory object
    MemoryObj *& mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(node);
        // we don't know the structure of the memory,
        // so use the unknown offset
        for (const Pointer& ptr : valist->getPointsTo()) {
            // actually, it should be only one pointer
            // so the for loop is over-kill
            assert(ptr.isKnown());
            // the offset should be 0 (this is pointer of alloca)
            assert(*ptr.offset == 0);
            // but we again use the UNKNOWN_OFFSET
            ptr.obj->addPointsTo(UNKNOWN_OFFSET, Pointer(mo, UNKNOWN_OFFSET));
        }
    }

    // copy points-to from the argument to the valist
    return mo->addPointsTo(UNKNOWN_OFFSET, vaparam->in->getPointsTo());
}

bool LLVMPointsToAnalysis::handleIntrinsicFunction(CallInst *Inst,
                                                   LLVMNode *node)
{
    IntrinsicInst *I = cast<IntrinsicInst>(Inst);
    if (isa<MemTransferInst>(I))
        return handleMemTransfer(I, node);
    else if (I->getIntrinsicID() == Intrinsic::vastart) {
        return handleVaStart(I, node);
    }

    return false;
}

bool LLVMPointsToAnalysis::handleCallInst(CallInst *Inst, LLVMNode *node)
{
    bool changed = false;
    int type;
    Type *Ty = Inst->getType();

    // TODO: we can match the patterns and at least
    // get some points-to information from inline asm.
    if (Inst->isInlineAsm())
        return false;

    Function *func
        = dyn_cast<Function>(Inst->getCalledValue()->stripPointerCasts());

    if (func && func->isIntrinsic())
        return handleIntrinsicFunction(Inst, node);

    // add subgraphs dynamically according the points-to information
    LLVMNode *calledFuncNode = getOperand(node, Inst->getCalledValue(), 0);
    if (!func && calledFuncNode)
        changed |= handleFunctionPtrCall(calledFuncNode, node);

    if (func && (type = getMemAllocationFunc(func)))
        return handleDynamicMemAllocation(Inst, node, type);

    // function is undefined and returns a pointer?
    // In that case create pointer to unknown location
    // and set this node to point to unknown location
    if ((!func || func->size() == 0)
         && !node->hasSubgraphs() && Ty->isPointerTy())
        return handleUndefinedReturnsPointer(Inst, node);

    for (LLVMDependenceGraph *sub : node->getSubgraphs()) {
        changed |= propagatePointersToArguments(sub, Inst, node);
        changed |= propagatePointersFromArguments(sub, node);
    }

    // what about llvm intrinsic functions like llvm.memset?
    // we could handle those

    return changed;
}

bool LLVMPointsToAnalysis::handleIntToPtr(IntToPtrInst *Inst,
                                          LLVMNode *node)
{
    (void) Inst;

    // FIXME this is sound, but unprecise - we can do more
    return node->addPointsTo(UnknownMemoryLocation);
}

bool LLVMPointsToAnalysis::handleBitCastInst(BitCastInst *Inst,
                                             LLVMNode *node)
{
    bool changed = false;
    LLVMNode *op = getOperand(node, Inst->stripPointerCasts(), 0);
    if (!op) {
        DBG("WARN: Cast without operand " << *Inst);
        return false;
    }

    if (!Inst->getType()->isPointerTy())
        return false;

    if (Inst->isLosslessCast()) {
        for (auto ptr : op->getPointsTo()) {
            changed |= node->addPointsTo(ptr);
        }
    } else
        DBG("WARN: Not a loss less cast unhandled" << *Inst);

    return changed;
}

bool LLVMPointsToAnalysis::handleReturnInst(ReturnInst *Inst,
                                            LLVMNode *node)
{
    bool changed = false;
    LLVMNode *val = node->getOperand(0);
    Value *llvmval;

    (void) Inst;

    if (!val)
        return false;

    llvmval = val->getKey();
    if (!llvmval->getType()->isPointerTy())
        return false;

    for (auto ptr : val->getPointsTo())
        changed |= node->addPointsTo(ptr);

    // call-site will take the values,
    // since we do not have references to parent
    // graphs

    return changed;
}

bool LLVMPointsToAnalysis::handlePHINode(llvm::PHINode *Phi,
                                         LLVMNode *node)
{
    if (!node->isPointerTy())
        return false;

    // since this points-to analysis is flow insensitive,
    // we just add all the pointers incoming to the points-to set
    size_t opnum = node->getOperandsNum();
    bool changed = false;

    for (unsigned i = 0; i < opnum; ++i) {
        LLVMNode *op = getOperand(node, Phi->getIncomingValue(i), i);
        assert(op && "Do not have an operand");

        for (const Pointer& p : op->getPointsTo())
            changed |= node->addPointsTo(p);
    }

    return changed;
}

bool LLVMPointsToAnalysis::handleSelectNode(llvm::SelectInst *Sel,
                                            LLVMNode *node)
{
    if (!node->isPointerTy())
        return false;

    bool changed = false;

    for (unsigned i = 0; i < 2; ++i) {
        LLVMNode *op = getOperand(node, Sel->getOperand(i + 1), i);
        assert(op && "Do not have an operand");

        for (const Pointer& p : op->getPointsTo())
            changed |= node->addPointsTo(p);
    }

    return changed;
}


static void propagateGlobalPointsToMain(LLVMDGParameters *params, LLVMDependenceGraph *dg)
{
    for (auto I = params->global_begin(), E = params->global_end(); I != E; ++I) {
        // points-to set is in the real global
        LLVMNode *glob = dg->getGlobalNode(I->first);
        assert(glob && "ERR: No global for parameter");

        PointsToSetT& S = glob->getPointsTo();
        LLVMNode *p = I->second.in;
        p->addPointsTo(S);

        // and also add a data dependence edge, so that we
        // have a connection between the real global and the parameter
        glob->addDataDependence(p);
    }
}

static void propagateGlobalPointsToMain(LLVMDependenceGraph *dg)
{
    LLVMDGParameters *p = dg->getParameters();
    if (p)
        propagateGlobalPointsToMain(p, dg);
}

void LLVMPointsToAnalysis::handleGlobals()
{
    // do we have the globals at all?
    if (!dg->getGlobalNodes())
        return;

    // add memory object to every global
    // do it in separate loop, because we need to
    // have these memory objects in place before we
    // set the points-to sets due to initialization (in next loop)
    for (auto& it : *dg->getGlobalNodes())
        handleGlobal(it.second);

    // initialize globals
    for (auto& it : *dg->getGlobalNodes()) {
        GlobalVariable *GV = dyn_cast<GlobalVariable>(it.first);
        // is it global variable or function?
        if (!GV)
            continue;

        if (GV->hasInitializer() && !GV->isExternallyInitialized()) {
            Constant *C = GV->getInitializer();
            uint64_t off = 0;
            Type *Ty;

            // we must handle ConstantExpr here, becaues the operand of the
            // ConstantExpr is the right object that we'd get in addGlobalPointsTo
            // using getConstantExprPointer(), but the offset would be wrong (always 0)
            // which can be broken e. g. with this C code:
            // const char *str = "Im ugly string" + 5;
            if (isa<ConstantExpr>(C) || isa<Function>(C))
                addGlobalPointsTo(C, it.second, off);
            else if (C->getType()->isAggregateType()) {
                for (auto I = C->op_begin(), E = C->op_end(); I != E; ++I) {
                    Value *val = *I;
                    Ty = val->getType();

                    if (Ty->isPointerTy())
                        addGlobalPointsTo(cast<Constant>(val), it.second, off);

                    off += DL->getTypeAllocSize(Ty);
                }
            } else if (isa<ConstantPointerNull>(C)) {
                MemoryObj *mo = it.second->getMemoryObj();
                assert(mo && "global has no memory object");
                mo->addPointsTo(Offset(0), NullPointer);
            } else if (!isa<ConstantInt>(C)) {
#ifdef DEBUG_ENABLED
                errs() << "ERR points-to: unhandled global initializer: "
                       << *C << "\n";
#endif
            }
        }
    }

    propagateGlobalPointsToMain(dg);
}

bool LLVMPointsToAnalysis::runOnNode(LLVMNode *node, LLVMNode *prev)
{
    bool changed = false;
    Value *val = node->getKey();
    (void) prev;

    if (isa<AllocaInst>(val)) {
        changed |= handleAllocaInst(node);
    } else if (StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        changed |= handleStoreInst(Inst, node);
    } else if (LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        changed |= handleLoadInst(Inst, node);
    } else if (GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        changed |= handleGepInst(Inst, node);
    } else if (CallInst *Inst = dyn_cast<CallInst>(val)) {
        changed |= handleCallInst(Inst, node);
    } else if (ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        changed |= handleReturnInst(Inst, node);
    } else if (IntToPtrInst *Inst = dyn_cast<IntToPtrInst>(val)) {
        changed |= handleIntToPtr(Inst, node);
    } else if (BitCastInst *Inst = dyn_cast<BitCastInst>(val)) {
        changed |= handleBitCastInst(Inst, node);
    } else if (PHINode *Inst = dyn_cast<PHINode>(val)) {
        changed |= handlePHINode(Inst, node);
    } else if (SelectInst *Inst = dyn_cast<SelectInst>(val)) {
        changed |= handleSelectNode(Inst, node);
    } else {
#ifdef DEBUG_ENABLED
        Instruction *I = dyn_cast<Instruction>(val);
        assert(I && "Not an Instruction?");

        if (I->mayReadOrWriteMemory())
            DBG("WARN: Unhandled instruction: " << *val);
#endif
    }

    return changed;
}

} // namespace analysis
} // namespace dg
