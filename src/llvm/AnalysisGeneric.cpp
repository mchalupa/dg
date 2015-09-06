
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

Pointer getConstantExprPointer(const ConstantExpr *CE,
                               LLVMDependenceGraph *dg,
                               const llvm::DataLayout *DL)
{
    Pointer pointer(nullptr, UNKNOWN_OFFSET);
    const Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();
    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
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
    } else if (const BitCastInst *BC = dyn_cast<BitCastInst>(Inst)) {
        if (BC->isLosslessCast()) {
            LLVMNode *op = getNode(dg, Inst->stripPointerCasts());
            if (!op) {
                errs() << "ERR: unsupported BitCast constant operand"
                       << *Inst << "\n";
            } else {
                pointer.obj = op->getMemoryObj();
                pointer.offset = 0;
            }
        } else
            errs() << "WARN: Not a loss less cast unhandled ConstExpr"
                   << *BC << "\n";
    } else {
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

    delete Inst;
    return pointer;
}



} // namespace analysis
} // namespace dg


