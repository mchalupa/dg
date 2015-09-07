
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

#include "AnalysisGeneric.h"
#include "LLVMDependenceGraph.h"
#include "PointsTo.h"

using namespace llvm;

namespace dg {
namespace analysis {

// make this method of LLVMDependenceGraph
static LLVMNode *getNode(LLVMDependenceGraph *dg, const llvm::Value *val)
{
    LLVMNode *n = dg->getNode(val);
    if (n)
        return n;

    if (val->getType()->isFunctionTy()) {
        n = new LLVMNode(val);
        MemoryObj *&mo = n->getMemoryObj();
        mo = new MemoryObj(n);
        mo->addPointsTo(0, Pointer(mo));
    }

    return n;
}

static Pointer handleConstantBitCast(LLVMDependenceGraph *dg, const BitCastInst *BC)
{
    Pointer pointer(nullptr, UNKNOWN_OFFSET);

    if (!BC->isLosslessCast()) {
        errs() << "WARN: Not a loss less cast unhandled ConstExpr"
               << *BC << "\n";
        return pointer;
    }

    const Value *llvmOp = BC->stripPointerCasts();
    LLVMNode *op = getNode(dg, llvmOp);
    if (!op) {
        if (isa<Function>(llvmOp)) {
            op = new LLVMNode(llvmOp);
            MemoryObj *&mo = op->getMemoryObj();
            mo = new MemoryObj(op);

            pointer.obj = mo;
            op->addPointsTo(pointer);
        } else {
            errs() << "ERR: unsupported BitCast constant operand"
                   << *BC << "\n";
        }
    } else {
        PointsToSetT& ptset = op->getPointsTo();
        if (ptset.size() != 1) {
            errs() << "ERR: constant BitCast with not only one pointer "
                   << *BC << "\n";
        } else
            pointer = *ptset.begin();
    }

    return pointer;
}

static Pointer handleConstantGep(LLVMDependenceGraph *dg,
                                 const GetElementPtrInst *GEP,
                                 const llvm::DataLayout *DL)
{
    Pointer pointer(nullptr, UNKNOWN_OFFSET);
    const Value *op = GEP->getPointerOperand();
    LLVMNode *opNode = dg->getNode(op);
    assert(opNode && "No node for Constant GEP operand");

    pointer.obj = opNode->getMemoryObj();

    APInt offset(64, 0);
    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(64))
            pointer.offset = offset.getZExtValue();
        else
            errs() << "WARN: Offset greater than 64-bit" << *GEP << "\n";
    }
    // else offset is set to UNKNOWN (in constructor)

    return pointer;
}

Pointer getConstantExprPointer(const ConstantExpr *CE,
                               LLVMDependenceGraph *dg,
                               const llvm::DataLayout *DL)
{
    Pointer pointer(nullptr, UNKNOWN_OFFSET);

    const Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();
    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
        pointer = handleConstantGep(dg, GEP, DL);
    } else if (const BitCastInst *BC = dyn_cast<BitCastInst>(Inst)) {
        pointer = handleConstantBitCast(dg, BC);
    } else {
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

    delete Inst;
    return pointer;
}



} // namespace analysis
} // namespace dg


