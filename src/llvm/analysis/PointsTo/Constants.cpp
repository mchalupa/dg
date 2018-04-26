#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

Pointer LLVMPointerSubgraphBuilder::handleConstantPtrToInt(const llvm::PtrToIntInst *P2I)
{
    using namespace llvm;

    const Value *llvmOp = P2I->getOperand(0);
    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant PtrToInt with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantIntToPtr(const llvm::IntToPtrInst *I2P)
{
    using namespace llvm;

    const Value *llvmOp = I2P->getOperand(0);
    if (isa<ConstantInt>(llvmOp)) {
        llvm::errs() << "IntToPtr with constant: " << *I2P << "\n";
        return PointerUnknown;
    }

    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant PtrToInt with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantAdd(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *op;
    const Value *val = nullptr;
    Offset off = Offset::UNKNOWN;

    // see createAdd() for details
    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
        val = Inst->getOperand(0);
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
        val = Inst->getOperand(1);
    } else {
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    if (val)
        off = getConstantValue(val);

    assert(op->pointsTo.size() == 1
           && "Constant add with not only one pointer");

    Pointer ptr = *op->pointsTo.begin();
    if (off.isUnknown())
        return Pointer(ptr.target, Offset::UNKNOWN);
    else
        return Pointer(ptr.target, ptr.offset + off);
}

Pointer LLVMPointerSubgraphBuilder::handleConstantArithmetic(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *op;

    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
    } else {
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    assert(op->pointsTo.size() == 1
           && "Constant add with not only one pointer");

    Pointer ptr = *op->pointsTo.begin();
    return Pointer(ptr.target, Offset::UNKNOWN);
}

Pointer LLVMPointerSubgraphBuilder::handleConstantBitCast(const llvm::BitCastInst *BC)
{
    using namespace llvm;

    if (!BC->isLosslessCast()) {
        errs() << "WARN: Not a loss less cast unhandled ConstExpr"
               << *BC << "\n";
        abort();
        return PointerUnknown;
    }

    const Value *llvmOp = BC->stripPointerCasts();
    // (possibly recursively) get the operand of this bit-cast
    PSNode *op = getOperand(llvmOp);
    assert(op->pointsTo.size() == 1
           && "Constant BitCast with not only one pointer");

    return *op->pointsTo.begin();
}

Pointer LLVMPointerSubgraphBuilder::handleConstantGep(const llvm::GetElementPtrInst *GEP)
{
    using namespace llvm;

    const Value *op = GEP->getPointerOperand();
    Pointer pointer(UNKNOWN_MEMORY, Offset::UNKNOWN);

    // get operand PSNode (this may result in recursive call,
    // if this gep is recursively defined)
    PSNode *opNode = getOperand(op);
    assert(opNode->pointsTo.size() == 1
           && "Constant node has more that 1 pointer");
    pointer = *(opNode->pointsTo.begin());

    unsigned bitwidth = getPointerBitwidth(DL, op);
    APInt offset(bitwidth, 0);

    // get offset of this GEP
    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth) && !pointer.offset.isUnknown())
            pointer.offset = offset.getZExtValue();
        else
            errs() << "WARN: Offset greater than "
                   << bitwidth << "-bit" << *GEP << "\n";
    }

    return pointer;
}

Pointer LLVMPointerSubgraphBuilder::getConstantExprPointer(const llvm::ConstantExpr *CE)
{
    using namespace llvm;

    Pointer pointer(UNKNOWN_MEMORY, Offset::UNKNOWN);
    Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();

    switch(Inst->getOpcode()) {
        case Instruction::GetElementPtr:
            pointer = handleConstantGep(cast<GetElementPtrInst>(Inst));
            break;
        //case Instruction::ExtractValue:
        //case Instruction::Select:
            break;
        case Instruction::BitCast:
        case Instruction::SExt:
        case Instruction::ZExt:
            pointer = handleConstantBitCast(cast<BitCastInst>(Inst));
            break;
        case Instruction::PtrToInt:
            pointer = handleConstantPtrToInt(cast<PtrToIntInst>(Inst));
            break;
        case Instruction::IntToPtr:
            pointer = handleConstantIntToPtr(cast<IntToPtrInst>(Inst));
            break;
        case Instruction::Add:
            pointer = handleConstantAdd(Inst);
            break;
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Trunc:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
            pointer = PointerUnknown;
            break;
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::SDiv:
            pointer = handleConstantArithmetic(Inst);
            break;
        default:
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

#if LLVM_VERSION_MAJOR < 5
    delete Inst;
#else
    Inst->deleteValue();
#endif
    return pointer;
}

PSNode *LLVMPointerSubgraphBuilder::createConstantExpr(const llvm::ConstantExpr *CE)
{
    Pointer ptr = getConstantExprPointer(CE);
    PSNode *node = PS.create(PSNodeType::CONSTANT, ptr.target, ptr.offset);

    addNode(CE, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createUnknown(const llvm::Value *val)
{
    // nothing better we can do, these operations
    // completely change the value of pointer...

    // FIXME: or there's enough unknown offset? Check it out!
    PSNode *node = PS.create(PSNodeType::CONSTANT, UNKNOWN_MEMORY, Offset::UNKNOWN);

    addNode(val, node);

    assert(node);
    return node;
}

} // namespace pta
} // namespace analysis
} // namespace dg

