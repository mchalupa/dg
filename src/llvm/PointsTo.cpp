#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDependenceGraph.h"
#include "PointsTo.h"

using namespace llvm;

namespace dg {
namespace analysis {

LLVMPointsToAnalysis::LLVMPointsToAnalysis(LLVMDependenceGraph *dg)
    : DataFlowAnalysis<LLVMNode>(dg->getEntryBB(), DATAFLOW_INTERPROCEDURAL),
      _dg(dg)
{
    handleGlobals();
}

static bool handleAllocaInst(const AllocaInst *Inst, LLVMNode *node)
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

static bool handleStoreInstPtr(LLVMNode *valNode, LLVMNode *ptrNode)
{
    bool changed = false;

    // iterate over points to locations of pointer node
    for (auto ptr : ptrNode->getPointsTo()) {
        // if we're storing a pointer, make obj[offset]
        // points to the same locations as the valNode
        for (auto valptr : valNode->getPointsTo())
            changed |= ptr.obj->addPointsTo(ptr.offset, valptr);
    }

    return changed;
}

static LLVMNode *findStoreInstVal(const llvm::Value *valOp, LLVMNode *node)
{
    LLVMNode *valNode = node->getOperand(1);

    if (!valNode) {
        if (isa<Argument>(valOp)) {
            LLVMDependenceGraph *dg = node->getDG();
            LLVMDGParameters *params = dg->getParameters();
            LLVMDGParameter *p = params->find(valOp);
            // we're storing value of parameter somewhere,
            // so it is input parameter
            if (p)
                valNode = p->in;
        }

        if (valNode)
            node->setOperand(valNode, 1);
    }

    return valNode;
}

static bool handleStoreInst(const StoreInst *Inst, LLVMNode *node)
{
    bool changed = false;
    const Value *valOp = Inst->getValueOperand();

    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode && "No node for pointer argument");

    LLVMNode *valNode = findStoreInstVal(valOp, node);
    if (!valNode) {
        if (!isa<Constant>(valOp))
            errs() << "ERR: no value node for " << *Inst << "\n";

        return false;
    }

    if (valOp->getType()->isPointerTy())
        changed |= handleStoreInstPtr(valNode, ptrNode);

    return changed;
}

static bool handleLoadInst(const LoadInst *Inst, LLVMNode *node)
{
    bool changed = false;
    LLVMNode *ptrNode = node->getOperand(0);
    assert(ptrNode && "No pointer operand for LoadInst");

    bool isptr = Inst->getType()->isPointerTy();
    if (!isptr)
        return false;

    // get values that are referenced by pointer and
    // store them as values of this load (or as pointsTo
    // if the load it pointer)
    for (auto ptr : ptrNode->getPointsTo()) {
        // load of pointer makes this node point
        // to the same values as ptrNode
        if (ptr.obj->isUnknown())
            changed |= node->addPointsTo(ptr);
        else {
            for (auto memptr : ptr.obj->pointsTo[ptr.offset])
                changed |= node->addPointsTo(memptr);

            if (ptr.obj->pointsTo.count(UNKNOWN_OFFSET) != 0)
                for (auto memptr : ptr.obj->pointsTo[UNKNOWN_OFFSET])
                    changed |= node->addPointsTo(memptr);
        }
    }

    return changed;
}

static bool handleGepInst(const GetElementPtrInst *Inst,
                          LLVMNode *node, LLVMNode *ptrNode)
{
    bool changed = false;
    const DataLayout& DL = node->getDG()->getModule()->getDataLayout();
    APInt offset(64, 0);
    Offset off;
    uint64_t size;
    Type *Ty;

    assert(ptrNode && "Do not have GEP ptr node");

    if (Inst->accumulateConstantOffset(DL, offset)) {
        if (offset.isIntN(64)) {
            for (auto ptr : ptrNode->getPointsTo()) {
                    if (ptr.obj->isUnknown() || ptr.offset.isUnknown())
                        // don't store unknown with different offsets,
                        changed |= node->addPointsTo(ptr.obj);
                    else {
                        off = offset.getZExtValue();
                        off += ptr.offset;
                        Ty = ptr.obj->node->getKey()->getType()->getContainedType(0);
                        size = DL.getTypeAllocSize(Ty);
                        // ivalid offset might mean we're cycling due to some
                        // cyclic dependency
                        if (*off >= size) {
                            errs() << "INFO: cropping GEP, off > size: "
                                   << *off << " " << size
                                   << " Type: " << *Ty << "\n";
                            changed |= node->addPointsTo(ptr.obj, UNKNOWN_OFFSET);
                        } else
                            changed |= node->addPointsTo(ptr.obj, off);
                    }
            }

            return changed;
        } else
            errs() << "WARN: GEP offset greater that 64-bit\n";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    for (auto ptr : ptrNode->getPointsTo())
        // UKNOWN_OFFSET + something is still unknown
        changed |= node->addPointsTo(ptr.obj, UNKNOWN_OFFSET);

    return changed;
}

static bool handleGepInst(const GetElementPtrInst *Inst, LLVMNode *node)
{
    LLVMNode *ptrNode = node->getOperand(0);
    return handleGepInst(Inst, node, ptrNode);
}

// handle const Gep in global variables
static bool handleConstGepInst(const GetElementPtrInst *Inst,
                               LLVMNode *node, LLVMNode *ptrNode)
{
    bool changed = false;
    const DataLayout& DL = node->getDG()->getModule()->getDataLayout();
    APInt offset(64, 0);
    Offset off;
    uint64_t size;
    Type *Ty;

    assert(ptrNode && "Do not have GEP ptr node");

    MemoryObj *mo = node->getMemoryObj();
    assert(mo && "Global has no mo");

    if (Inst->accumulateConstantOffset(DL, offset)) {
        if (offset.isIntN(64)) {
            for (auto ptr : ptrNode->getPointsTo()) {
                    if (ptr.obj->isUnknown() || ptr.offset.isUnknown())
                        // don't store unknown with different offsets,
                        changed |= mo->addPointsTo(0, Pointer(ptr.obj, UNKNOWN_OFFSET));
                    else {
                        off = offset.getZExtValue();
                        off += ptr.offset;
                        Ty = ptr.obj->node->getKey()->getType()->getContainedType(0);
                        size = DL.getTypeAllocSize(Ty);
                        // ivalid offset might mean we're cycling due to some
                        // cyclic dependency
                        if (*off >= size) {
                            errs() << "INFO: cropping GEP, off > size: "
                                   << *off << " " << size
                                   << " Type: " << *Ty << "\n";
                            changed |= mo->addPointsTo(0, Pointer(ptr.obj, UNKNOWN_OFFSET));
                        } else
                            changed |= mo->addPointsTo(0, Pointer(ptr.obj, ptr.offset + off));
                    }
            }

            return changed;
        } else
            errs() << "WARN: GEP offset greater that 64-bit\n";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    for (auto ptr : ptrNode->getPointsTo())
        // UKNOWN_OFFSET + something is still unknown
        changed |= mo->addPointsTo(0, Pointer(ptr.obj, UNKNOWN_OFFSET));

    return changed;
}



static bool handleConstantExpr(const ConstantExpr *CE, LLVMNode *node)
{
    bool changed = false;

    if (CE->getOpcode() == Instruction::GetElementPtr) {
        const Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();
        LLVMNode *ptrNode = node->getDG()->getNode(*CE->op_begin());
        //changed |= handleGepInst(cast<GetElementPtrInst>(Inst), node, ptrNode);
        handleConstGepInst(cast<GetElementPtrInst>(Inst), node, ptrNode);
        delete Inst;
    } else
        errs() << "ERR: unhandled ConstantExpr: " << *CE << "\n";

    return changed;
}

static bool handleCallInst(const CallInst *Inst, LLVMNode *node)
{
    bool changed = false;
    Type *Ty = Inst->getType();
    bool isptr;

    // function is undefined?
    if (!node->hasSubgraphs() && Ty->isPointerTy()) {
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


static bool handleBitCastInst(const BitCastInst *Inst, LLVMNode *node)
{
    bool changed = false;
    LLVMNode *op = node->getOperand(0);
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

static bool handleReturnInst(const ReturnInst *Inst, LLVMNode *node)
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
    if (!_dg->ownsGlobalNodes())
        return;

    for (auto it : *_dg->getGlobalNodes())
        handleGlobal(it.first, it.second);

    bool changed = false;
    do {
        for (auto it : *_dg->getGlobalNodes()) {
            const GlobalVariable *GV = cast<GlobalVariable>(it.first);
            if (GV->hasInitializer()) {
                const Constant *C = GV->getInitializer();
                if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C))
                    changed |= handleConstantExpr(CE, it.second);
            }
        }
    } while (changed);
}

bool LLVMPointsToAnalysis::runOnNode(LLVMNode *node)
{
    bool changed = false;
    const Value *val = node->getKey();

    if (const AllocaInst *Inst = dyn_cast<AllocaInst>(val)) {
        changed |= handleAllocaInst(Inst, node);
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
        const Instruction *I = dyn_cast<Instruction>(val);
        assert(I && "Not an Instruction?");

        if (I->mayReadOrWriteMemory())
            errs() << "WARN: Unhandled instruction: " << *val << "\n";
    }

    return changed;
}

} // namespace analysis
} // namespace dg
