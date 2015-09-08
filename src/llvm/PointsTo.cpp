#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDependenceGraph.h"
#include "PointsTo.h"
#include "AnalysisGeneric.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMPointsToAnalysis::LLVMPointsToAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      dg(dg)
{
    Module *m = dg->getModule();
    // set data layout
    DL = m->getDataLayout();

    handleGlobals();
}

static bool handleMemAllocation(LLVMNode *node)
{
    // every global is a pointer
    MemoryObj *&mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(node);
        node->addPointsTo(mo);
        return true;
    }

    return false;
}

bool LLVMPointsToAnalysis::handleAllocaInst(LLVMNode *node)
{
    return handleMemAllocation(node);
}

static bool handleGlobal(const Value *Inst, LLVMNode *node)
{
    // we don't care about non pointers right now
    if (!Inst->getType()->isPointerTy())
        return false;

    return handleMemAllocation(node);
}

LLVMNode *LLVMPointsToAnalysis::getOperand(LLVMNode *node,
                                           const Value *val, unsigned int idx)
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

bool LLVMPointsToAnalysis::handleStoreInst(const StoreInst *Inst, LLVMNode *node)
{
    // get ptrNode before checking if value type is pointer type,
    // because the pointer operand can be ConstantExpr and in getOperand()
    // we resolve its points-to set
    LLVMNode *ptrNode = getOperand(node, Inst->getPointerOperand(), 0);

    const Value *valOp = Inst->getValueOperand();
    if (!valOp->getType()->isPointerTy())
        return false;

    LLVMNode *valNode = getOperand(node, valOp, 1);
    assert(ptrNode && "No ptr node");
    assert(valNode && "No val node");

    return handleStoreInstPtr(valNode, ptrNode);
}

Pointer LLVMPointsToAnalysis::getConstantExprPointer(const ConstantExpr *CE)
{
    return dg::analysis::getConstantExprPointer(CE, dg, DL);
}

static bool handleLoadInstPtr(const Pointer& ptr, LLVMNode *node)
{
    bool changed = false;

    // load of pointer makes this node point
    // to the same values as ptrNode
    if (ptr.isNull() || ptr.obj->isUnknown())
        changed |= node->addPointsTo(ptr);
    else {
        for (auto memptr : ptr.obj->pointsTo[ptr.offset])
            changed |= node->addPointsTo(memptr);

        if (ptr.obj->pointsTo.count(UNKNOWN_OFFSET) != 0)
            for (auto memptr : ptr.obj->pointsTo[UNKNOWN_OFFSET])
                changed |= node->addPointsTo(memptr);
    }

    return changed;
}

static bool handleLoadInstPointsTo(LLVMNode *ptrNode, LLVMNode *node)
{
    bool changed = false;

    // get values that are referenced by pointer and
    // store them as values of this load (or as pointsTo
    // if the load it pointer)
    for (auto ptr : ptrNode->getPointsTo())
        changed |= handleLoadInstPtr(ptr, node);

    return changed;
}

bool LLVMPointsToAnalysis::handleLoadInst(const LoadInst *Inst, LLVMNode *node)
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
    const Value *ptrVal;
    Type *Ty;

    for (auto ptr : ptrNode->getPointsTo()) {
        if (ptr.obj->isUnknown() || ptr.offset.isUnknown())
            // don't store unknown with different offsets,
            changed |= node->addPointsTo(ptr.obj);
        else {
            ptrVal = ptr.obj->node->getKey();
            Ty = ptrVal->getType()->getContainedType(0);

            off += ptr.offset;
            size = DL->getTypeAllocSize(Ty);

            // ivalid offset might mean we're cycling due to some
            // cyclic dependency
            if (*off >= size) {
                errs() << "INFO: cropping GEP, off > size: " << *off
                       << " " << size << " Type: " << *Ty << "\n";
                changed |= node->addPointsTo(ptr.obj, UNKNOWN_OFFSET);
            } else
                changed |= node->addPointsTo(ptr.obj, off);
        }
    }

    return changed;
}

bool LLVMPointsToAnalysis::handleGepInst(const GetElementPtrInst *Inst,
                                         LLVMNode *node)
{
    bool changed = false;
    APInt offset(64, 0);

    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode && "Do not have GEP ptr node");

    if (Inst->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(64))
            return addPtrWithOffset(ptrNode, node, offset.getZExtValue(), DL);
        else
            errs() << "WARN: GEP offset greater that 64-bit\n";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    for (auto ptr : ptrNode->getPointsTo())
        // UKNOWN_OFFSET + something is still unknown
        changed |= node->addPointsTo(ptr.obj, UNKNOWN_OFFSET);

    return changed;
}

// @CE is ConstantExpr initializer
// @node is global's node
bool LLVMPointsToAnalysis::addGlobalPointsTo(const Constant *C,
                                             LLVMNode *node,
                                             uint64_t off)
{
    Pointer ptr (nullptr, 0);
    MemoryObj *mo = node->getMemoryObj();
    assert(mo && "Global has no mo");

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
        ptr = getConstantExprPointer(CE);
    } else if (isa<ConstantPointerNull>(C)) {
        // pointer is null already, do nothing
    } else {
        // it is a pointer to somewhere (we check that it is a pointer
        // before calling this method), so just get where
        LLVMNode *ptrNode = dg->getNode(C);
        assert(ptrNode && "Do not have node for  pointer initializer of global");

        ptr.obj = ptrNode->getMemoryObj();
    }

    return mo->addPointsTo(off, ptr);
}

static void add_bb(LLVMBBlock *BB, LLVMPointsToAnalysis *PA)
{
    PA->addBB(BB);
}

// add subghraph BBs to data-flow analysis
// (needed if we create a graph due to the function pointer)
static void addSubgraphBBs(LLVMPointsToAnalysis *PA,
                           LLVMDependenceGraph *graph)
{
    BBlockDFS<LLVMNode> dfs;
    dfs.run(graph->getEntryBB(), add_bb, PA);
}

static bool handleFunctionPtrCall(const Function *func,
                                  LLVMNode *calledFuncNode,
                                  LLVMNode *node, LLVMPointsToAnalysis *PA)
{
    bool changed = false;

    for (const Pointer& ptr : calledFuncNode->getPointsTo()) {
        if (ptr.isNull() || ptr.obj->isUnknown()) {
            errs() << "ERR: CallInst wrong func pointer\n";
            continue;
        }

        const Function *func = cast<Function>(ptr.obj->node->getValue());
        LLVMDependenceGraph *dg = node->getDG();
        LLVMDependenceGraph *subg = dg->getSubgraph(func);
        if (!subg) {
            subg = dg->buildSubgraph(node, func);

            LLVMNode *entry = subg->getEntry();
            dg->addGlobalNode(entry);
            handleGlobal(func, entry);

            addSubgraphBBs(PA, subg);
            changed = true;
        }

        node->addActualParameters(subg, func);
        changed |= node->addSubgraph(subg);
    }

    return changed;
}

static bool isMemAllocationFunc(const Function *func)
{
    if (!func || !func->hasName())
        return false;

    const char *name = func->getName().data();
    if (strcmp(name, "malloc") == 0 ||
        strcmp(name, "calloc") == 0)
        return true;

    // realloc should overtake the memory object from former pointer

    return false;
}

static bool handleUndefinedReturnsPointer(const CallInst *Inst, LLVMNode *node)
{
    LLVMNode *target = nullptr;
    // is it call via function pointer, or it is just undefined function?
    LLVMNode *op = node->getOperand(0);
    if (op) {
        // function pointer! check if we can be malloc and similar
        for (const Pointer& ptr : op->getPointsTo()) {
            if (ptr.isNull() || ptr.obj->isUnknown()) {
                errs() << "ERR: wrong pointer " << *Inst << "\n";
                continue;
            }

            const Function *func = dyn_cast<Function>(ptr.obj->node->getKey());
            assert(func && "function pointer contains non-function val");
            if (isMemAllocationFunc(func)) {
                target = node;
                break;
            }
        }
    }

    // ok, undefined function - point to unknown memory
    MemoryObj *& mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(target);
        node->addPointsTo(mo);
        return true;
    }

    return false;
}

bool propagatePointersToArguments(LLVMDependenceGraph *subgraph, LLVMNode *callNode)
{
    bool changed = false;
    LLVMDGParameters *formal = subgraph->getParameters();
    // we check if the function has arguments before going here,
    // so this would be a bug
    assert(formal && "no formal arguments");

    const Function *subfunc = dyn_cast<Function>(subgraph->getEntry()->getKey());
    assert(subfunc && "Entry is not a llvm::Function");

    // handle values for arguments
    // argument 0 is the called function, so start from 1
    int i = 1;
    for (auto I = subfunc->arg_begin(), E = subfunc->arg_end();
         I != E; ++I, ++i) {
        if (!I->getType()->isPointerTy())
            continue;

        LLVMDGParameter *p = formal->find(&*I);
        if (!p) {
            errs() << "ERR: no such formal param: " << *I << "\n";
            continue;
        }

        LLVMNode *op = callNode->getOperand(i);
        if (!op) {
            errs() << "ERR: no operand for actual param of formal param: "
                   << *I << "\n";
            continue;
        }

        for (const Pointer& ptr : op->getPointsTo())
            changed |= p->in->addPointsTo(ptr);
    }

    if (!callNode->isPointerTy())
        return changed;

    // handle return values
    LLVMNode *retval = subgraph->getExit();
    // this is artificial return value, the real
    // are control dependent on it
    for (auto I = retval->rev_control_begin(), E = retval->rev_control_end();
         I != E; ++I) {
        // we should iterate only over return inst
        assert(isa<ReturnInst>((*I)->getKey()));

        for (auto ptr : (*I)->getPointsTo())
            changed |= callNode->addPointsTo(ptr);
    }

    return changed;
}

bool LLVMPointsToAnalysis::handleCallInst(const CallInst *Inst, LLVMNode *node)
{
    bool changed = false;
    Type *Ty = Inst->getType();
    Function *func = Inst->getCalledFunction();

    // function is undefined and returns a pointer?
    // In that case create pointer to unknown location
    // and set this node to point to unknown location
    if (!func && !node->hasSubgraphs() && Ty->isPointerTy())
        return handleUndefinedReturnsPointer(Inst, node);

    if (isMemAllocationFunc(func))
        return handleMemAllocation(node);

    LLVMNode *calledFuncNode = node->getOperand(0);
    // add subgraphs dynamically according the points-to information
    if (!func && calledFuncNode)
        changed |= handleFunctionPtrCall(func, calledFuncNode, node, this);

    // if this function has no arguments, we can bail out here
    if (Inst->getNumArgOperands() == 0)
        return changed;

    for (LLVMDependenceGraph *sub : node->getSubgraphs())
        changed |= propagatePointersToArguments(sub, node);

    // what about llvm intrinsic functions like llvm.memset?
    // we could handle those

    return changed;
}

bool LLVMPointsToAnalysis::handleBitCastInst(const BitCastInst *Inst, LLVMNode *node)
{
    bool changed = false;
    LLVMNode *op = getOperand(node, Inst->stripPointerCasts(), 0);
    if (!op) {
        errs() << "WARN: Cast without operand " << *Inst << "\n";
        return false;
    }

    if (!Inst->getType()->isPointerTy())
        return false;

    if (Inst->isLosslessCast()) {
        for (auto ptr : op->getPointsTo()) {
            changed |= node->addPointsTo(ptr);
        }
    } else
        errs() << "WARN: Not a loss less cast unhandled" << *Inst << "\n";

    return changed;
}

bool LLVMPointsToAnalysis::handleReturnInst(const ReturnInst *Inst, LLVMNode *node)
{
    bool changed = false;
    LLVMNode *val = node->getOperand(0);
    const Value *llvmval;

    (void) Inst;

    if (!val)
        return false;

    llvmval = val->getKey();
    if (!llvmval->getType()->isPointerTy())
        return false;

    if (val->hasUnknownValue())
        return node->setUnknownValue();

    for (auto ptr : val->getPointsTo())
        changed |= node->addPointsTo(ptr);

    // call-site will take the values,
    // since we do not have references to parent
    // graphs

    return changed;
}

void LLVMPointsToAnalysis::handleGlobals()
{
    // do we have the globals at all?
    if (!dg->ownsGlobalNodes())
        return;

    for (auto it : *dg->getGlobalNodes())
        handleGlobal(it.first, it.second);

    // initialize globals
    for (auto it : *dg->getGlobalNodes()) {
        const GlobalVariable *GV = dyn_cast<GlobalVariable>(it.first);
        // is it global variable or function?
        if (!GV)
            continue;

        if (GV->hasInitializer() && !GV->isExternallyInitialized()) {
            const Constant *C = GV->getInitializer();
            uint64_t off = 0;
            Type *Ty;

            // we must handle ConstantExpr here, becaues the operand of the
            // ConstantExpr is the right object that we'd get in addGlobalPointsTo
            // using getConstantExprPointer(), but the offset would be wrong (always 0)
            // which can be broken e. g. with this C code:
            // const char *str = "Im ugly string" + 5;
            if (isa<ConstantExpr>(C))
                addGlobalPointsTo(C, it.second, off);
            else if (C->getType()->isAggregateType()) {
                for (auto I = C->op_begin(), E = C->op_end(); I != E; ++I) {
                    const Value *val = *I;
                    Ty = val->getType();

                    if (Ty->isPointerTy()) {
                        addGlobalPointsTo(cast<Constant>(val), it.second, off);
                    }

                    off += DL->getTypeAllocSize(Ty);
                }
            }
        }
    }
}

bool LLVMPointsToAnalysis::runOnNode(LLVMNode *node)
{
    bool changed = false;
    const Value *val = node->getKey();

    if (isa<AllocaInst>(val)) {
        changed |= handleAllocaInst(node);
    } else if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        changed |= handleStoreInst(Inst, node);
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        changed |= handleLoadInst(Inst, node);
    } else if (const GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        changed |= handleGepInst(Inst, node);
    } else if (const CallInst *Inst = dyn_cast<CallInst>(val)) {
        changed |= handleCallInst(Inst, node);
    } else if (const ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        changed |= handleReturnInst(Inst, node);
    } else if (const BitCastInst *Inst = dyn_cast<BitCastInst>(val)) {
        changed |= handleBitCastInst(Inst, node);
    } else {
#ifdef DEBUG_ENABLED
        const Instruction *I = dyn_cast<Instruction>(val);
        assert(I && "Not an Instruction?");

        if (I->mayReadOrWriteMemory())
            errs() << "WARN: Unhandled instruction: " << *val << "\n";
#endif
    }

    return changed;
}

} // namespace analysis
} // namespace dg
