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

bool LLVMPointsToAnalysis::handleAllocaInst(LLVMNode *node)
{
    // every global is a pointer
    MemoryObj *& mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(node);
        node->addPointsTo(mo);
        return true;
    }

    return false;
}

LLVMNode *LLVMPointsToAnalysis::getOperand(LLVMNode *node,
                                           const Value *val, unsigned int idx)
{
    // ok, before calling this we call llvm::Value::getOperand() to get val
    // and in node->getOperand() we call it too. It is small overhead, but just
    // to know where to optimize when going to extrems

    LLVMNode *op = node->getOperand(idx);
    if (op)
        return op;

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(val)) {
        op = new LLVMNode(val);
        // FIXME add these nodes somewhere,
        // so that we can delete them later

        // set points-to sets
        Pointer ptr = getConstantExprPointer(CE);
        //MemoryObj *&mo = op->getMemoryObj();
        //mo = new MemoryObj(op);
        op->addPointsTo(ptr);
    } else if (isa<Function>(val)) {
        op = new LLVMNode(val);
        MemoryObj *&mo = op->getMemoryObj();
        mo = new MemoryObj(op);
        op->addPointsTo(Pointer(mo));
    } else if (isa<Argument>(val)) {
        // get dg of this graph, because we can be in subprocedure
        LLVMDependenceGraph *thisdg = node->getDG();
        LLVMDGParameters *params = thisdg->getParameters();
        if (!params) {
            // This is probably not an argument from out dg?
            // Is it possible? Or there's a bug
            errs() << "No params for dg with argument: " << *val << "\n";
            abort();
        }

        LLVMDGParameter *p = params->find(val);

        // XXX is it always the input param?
        if (p)
            op = p->in;
    } else if (isa<ConstantPointerNull>(val)) {
        // what to do with nullptr?
        op = new LLVMNode(val);
    } else {
        errs() << "ERR: Unsupported operand: " << *val << "\n";
        abort();
    }

    assert(op && "Did not set op");

    // set new operand
    node->setOperand(op, idx);

    return op;
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

bool LLVMPointsToAnalysis::handleCallInst(const CallInst *Inst, LLVMNode *node)
{
    bool changed = false;
    Type *Ty = Inst->getType();
    bool isptr;
    Function *func = Inst->getCalledFunction();

    // function is undefined and returns a pointer?
    // In that case create pointer to unknown location
    // and set this node to point to unknown location
    if (!func && !node->hasSubgraphs() && Ty->isPointerTy()) {
        MemoryObj *& mo = node->getMemoryObj();
        if (!mo) {
            mo = new MemoryObj(nullptr);
            node->addPointsTo(mo);
            return true;
        }

        return false;
    }

    LLVMNode **operands = node->getOperands();

    for (auto sub : node->getSubgraphs()) {
        LLVMDGParameters *formal = sub->getParameters();
        if (!formal) // no arguments
            continue;

        const Function *subfunc = dyn_cast<Function>(sub->getEntry()->getKey());
        assert(subfunc && "Entry is not a llvm::Function");

        // handle values for arguments
        int i = 1;
        for (auto I = subfunc->arg_begin(), E = subfunc->arg_end();
             I != E; ++I, ++i) {
            isptr = I->getType()->isPointerTy();
            if (!isptr)
                continue;

            LLVMDGParameter *p = formal->find(&*I);
            if (!p) {
                errs() << "ERR: no such formal param: " << *I << "\n";
                continue;
            }

            LLVMNode *op = operands[i];
            if (!op) {
                errs() << "ERR: no operand for actual param of formal param: "
                       << *I << "\n";
                continue;
            }

            for (const Pointer& ptr : op->getPointsTo())
                changed |= p->in->addPointsTo(ptr);
        }

        if (!Ty->isPointerTy())
            return changed;

        // handle return values
        LLVMNode *retval = sub->getExit();
        // this is artificial return value, the real
        // are control dependent on it
        for (auto I = retval->rev_control_begin(), E = retval->rev_control_end();
             I != E; ++I) {
            // we should iterate only over return inst
            assert(isa<ReturnInst>((*I)->getKey()));

            for (auto ptr : (*I)->getPointsTo())
                changed |= node->addPointsTo(ptr);
        }
    }

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

static bool handleGlobal(const Value *Inst, LLVMNode *node)
{
    // we don't care about non pointers right now
    if (!Inst->getType()->isPointerTy())
        return false;

    // every global points to some memory
    MemoryObj *& mo = node->getMemoryObj();
    if (!mo) {
        mo = new MemoryObj(node);
        node->addPointsTo(mo);
        return true;
    }

    return false;
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
        const GlobalVariable *GV = cast<GlobalVariable>(it.first);
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
