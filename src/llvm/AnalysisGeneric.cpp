
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

// pointer points to unknown memory location
MemoryObj UnknownMemoryObject(nullptr);
// unknown pointer value
Pointer UnknownMemoryLocation(&UnknownMemoryObject, 0);

bool Pointer::isUnknown() const
{
    return this == &UnknownMemoryLocation;
}

bool Pointer::pointsToUnknown() const
{
    assert(obj && "Pointer has not any memory object set");
    return obj->isUnknown();
}

bool Pointer::isKnown() const
{
    return !isUnknown() && !pointsToUnknown();
}

bool MemoryObj::isUnknown() const
{
    return this == &UnknownMemoryObject;
}

static LLVMNode *createNodeWithMemAlloc(const Value *val)
{
    LLVMNode *n = new LLVMNode(val);
    MemoryObj *&mo = n->getMemoryObj();
    mo = new MemoryObj(n);
    n->addPointsTo(Pointer(mo));

    return n;
}

static LLVMNode *getOrCreateNode(LLVMDependenceGraph *dg, const Value *val)
{
    LLVMNode *n = dg->getNode(val);
    if (n)
        return n;

    if (llvm::isa<llvm::Function>(val)) {
        n = createNodeWithMemAlloc(val);
    } else
        errs() << "ERR: unhandled not to create " << *val << "\n";

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
    LLVMNode *op = getOrCreateNode(dg, llvmOp);
    if (!op) {
        errs() << "ERR: unsupported BitCast constant operand" << *BC << "\n";
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

/*
 * we have DependenceGraph::getNode() which retrives existing node.
 * The operand nodes may not exists, though.
 * This function gets the existing node, or creates new one and sets
 * it as an operand.
 */
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
        // FIXME add these nodes somewhere,
        // so that we can delete them later
        op = new LLVMNode(val);

        // set points-to sets
        Pointer ptr = getConstantExprPointer(CE, dg, DL);
        op->addPointsTo(ptr);
    } else if (isa<Function>(val)) {
        // if the function was created via function pointer during
        // points-to analysis, the operand may not be not be set.
        // What is worse, the function may not be created either,
        // so the node just may not exists at all, so we need to
        // create it
        op = getOrCreateNode(dg, val);
    } else if (isa<ConstantPointerNull>(val)) {
        // what to do with nullptr?
        op = createNodeWithMemAlloc(val);
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


