
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

    if (llvm::isa<llvm::Function>(val)) {
        n = new LLVMNode(val);
        MemoryObj *&mo = n->getMemoryObj();
        mo = new MemoryObj(n);
        n->addPointsTo(Pointer(mo));
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
        // XXX this is covered by getNode isn't it?
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

LLVMNode *getOperand(LLVMNode *node, const llvm::Value *val,
                     unsigned int idx, const llvm::DataLayout *DL)
{
    // ok, before calling this we call llvm::Value::getOperand() to get val
    // and in node->getOperand() we call it too. It is small overhead, but just
    // to know where to optimize when going to extrems

    LLVMNode *op = node->getOperand(idx);
    if (op)
        return op;

    using namespace llvm;
    LLVMDependenceGraph *dg = node->getDG();

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(val)) {
        op = new LLVMNode(val);
        // FIXME add these nodes somewhere,
        // so that we can delete them later

        // set points-to sets
        Pointer ptr = getConstantExprPointer(CE, dg, DL);
        //MemoryObj *&mo = op->getMemoryObj();
        //mo = new MemoryObj(op);
        op->addPointsTo(ptr);
    } else if (isa<Function>(val)) {
        // if the function was created via function pointer during
        // points-to analysis, it may not be set
        op = getNode(dg, val);
    } else if (isa<Argument>(val)) {
        // get dg of this graph, because we can be in subprocedure
        LLVMDGParameters *params = dg->getParameters();
        if (!params) {
            // This is probably not an argument from out dg?
            // Is it possible? Or there's a bug
            errs() << "No params for dg with argument: " << *val << "\n";
            abort();
        }

        LLVMDGParameter *p = params->find(val);
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

} // namespace analysis
} // namespace dg


