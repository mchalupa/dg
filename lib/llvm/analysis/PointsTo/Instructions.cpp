#include "dg/llvm/analysis/PointsTo/PointerSubgraph.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace analysis {
namespace pta {

PSNode *LLVMPointerSubgraphBuilder::createAlloc(const llvm::Instruction *Inst)
{
    PSNodeAlloc *node = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
    addNode(Inst, node);

    const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(Inst);
    if (AI)
        node->setSize(getAllocatedSize(AI, DL));

    return node;
}

PSNode * LLVMPointerSubgraphBuilder::createLifetimeEnd(const llvm::Instruction *Inst)
{
    PSNode *op1 = getOperand(Inst->getOperand(1));
    PSNode *node = PS.create(PSNodeType::INVALIDATE_OBJECT, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createStore(const llvm::Instruction *Inst)
{
    const llvm::Value *valOp = Inst->getOperand(0);

    PSNode *op1 = getOperand(valOp);
    PSNode *op2 = getOperand(Inst->getOperand(1));

    PSNode *node = PS.create(PSNodeType::STORE, op1, op2);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createLoad(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSNode *op1 = getOperand(op);
    PSNode *node = PS.create(PSNodeType::LOAD, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createGEP(const llvm::Instruction *Inst)
{
    using namespace llvm;

    const GetElementPtrInst *GEP = cast<GetElementPtrInst>(Inst);
    const Value *ptrOp = GEP->getPointerOperand();
    unsigned bitwidth = getPointerBitwidth(DL, ptrOp);
    APInt offset(bitwidth, 0);

    PSNode *node = nullptr;
    PSNode *op = getOperand(ptrOp);

    if (*_options.fieldSensitivity > 0
        && GEP->accumulateConstantOffset(*DL, offset)) {
        // is offset in given bitwidth?
        if (offset.isIntN(bitwidth)) {
            // is 0 < offset < field_sensitivity ?
            uint64_t off = offset.getLimitedValue(*_options.fieldSensitivity);
            if (off == 0 || off < *_options.fieldSensitivity)
                node = PS.create(PSNodeType::GEP, op, offset.getZExtValue());
        } else
            errs() << "WARN: GEP offset greater than " << bitwidth << "-bit";
            // fall-through to Offset::UNKNOWN in this case
    }

    // we didn't create the node with concrete offset,
    // in which case we are supposed to create a node
    // with Offset::UNKNOWN
    if (!node)
        node = PS.create(PSNodeType::GEP, op, Offset::UNKNOWN);

    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createSelect(const llvm::Instruction *Inst)
{
    // with ptrtoint/inttoptr it may not be a pointer
    // assert(Inst->getType()->isPointerTy() && "BUG: This select is not a pointer");

    // select <cond> <op1> <op2>
    PSNode *op1 = getOperand(Inst->getOperand(1));
    PSNode *op2 = getOperand(Inst->getOperand(2));

    // select works as a PHI in points-to analysis
    PSNode *node = PS.create(PSNodeType::PHI, op1, op2, nullptr);
    addNode(Inst, node);

    assert(node);
    return node;
}

Offset accumulateEVOffsets(const llvm::ExtractValueInst *EV,
                           const llvm::DataLayout& DL) {
    Offset off{0};
    llvm::CompositeType *type
        = llvm::dyn_cast<llvm::CompositeType>(EV->getAggregateOperand()->getType());
    assert(type && "Don't have composite type in extractvalue");

    for (unsigned idx : EV->getIndices()) {
        assert(type->indexValid(idx) && "Invalid index");
        if (llvm::StructType *STy = llvm::dyn_cast<llvm::StructType>(type)) {
            const llvm::StructLayout *SL = DL.getStructLayout(STy);
            off += SL->getElementOffset(idx);
        } else {
            // array or vector, so just move in the array
            auto seqTy = llvm::cast<llvm::SequentialType>(type);
            off += idx + DL.getTypeAllocSize(seqTy->getElementType());
        }

        type = llvm::dyn_cast<llvm::CompositeType>(type->getTypeAtIndex(idx));
        if (!type)
            break; // we're done
    }

    return off;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createExtract(const llvm::Instruction *Inst)
{
    using namespace llvm;

    const ExtractValueInst *EI = cast<ExtractValueInst>(Inst);

    // extract <agg> <idx> {<idx>, ...}
    PSNode *op1 = getOperand(EI->getAggregateOperand());
    PSNode *G = PS.create(PSNodeType::GEP, op1, accumulateEVOffsets(EI, *DL));
    PSNode *L = PS.create(PSNodeType::LOAD, G);

    G->addSuccessor(L);

    PSNodesSeq ret = PSNodesSeq(G, L);
    addNode(Inst, ret);

    return ret;
}

PSNode *LLVMPointerSubgraphBuilder::createPHI(const llvm::Instruction *Inst)
{
    PSNode *node = PS.create(PSNodeType::PHI, nullptr);
    addNode(Inst, node);

    // NOTE: we didn't add operands to PHI node here, but after building
    // the whole function, because some blocks may not have been built
    // when we were creating the phi node

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createCast(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSNode *op1 = getOperand(op);
    PSNode *node = PS.create(PSNodeType::CAST, op1);

    addNode(Inst, node);

    assert(node);
    return node;
}

// ptrToInt work just as a bitcast
PSNode *LLVMPointerSubgraphBuilder::createPtrToInt(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSNode *op1 = getOperand(op);
    // NOTE: we don't support arithmetic operations, so instead of
    // just casting the value do gep with unknown offset -
    // this way we cover any shift of the pointer due to arithmetic
    // operations
    // PSNode *node = PS.create(PSNodeType::CAST, op1);
    PSNode *node = PS.create(PSNodeType::GEP, op1, 0);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createIntToPtr(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSNode *op1;

    if (llvm::isa<llvm::Constant>(op)) {
        llvm::errs() << "PTA warning: IntToPtr with constant: "
                     << *Inst << "\n";
        // if this is inttoptr with constant, just make the pointer
        // unknown
        op1 = UNKNOWN_MEMORY;
    } else
        op1 = getOperand(op);

    PSNode *node = PS.create(PSNodeType::CAST, op1);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createAdd(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *node;
    PSNode *op;
    const Value *val = nullptr;
    uint64_t off = Offset::UNKNOWN;

    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
        val = Inst->getOperand(0);
    } else if (isa<ConstantInt>(Inst->getOperand(1))) {
        op = getOperand(Inst->getOperand(0));
        val = Inst->getOperand(1);
    } else {
        // the operands are both non-constant. Check if we
        // can get an operand as one of them and if not,
        // fall-back to unknown memory, because we
        // would need to track down both operads...
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    assert(op && "Don't have operand for add");
    if (val)
        off = getConstantValue(val);

    node = PS.create(PSNodeType::GEP, op, off);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createArithmetic(const llvm::Instruction *Inst)
{
    using namespace llvm;

    PSNode *node;
    PSNode *op;

    // we don't know if the operand is the first or
    // the other operand
    if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(1));
    } else if (isa<ConstantInt>(Inst->getOperand(0))) {
        op = getOperand(Inst->getOperand(0));
    } else {
        // the operands are both non-constant. Check if we
        // can get an operand as one of them and if not,
        // fall-back to unknown memory, because we
        // would need to track down both operads...
        op = tryGetOperand(Inst->getOperand(0));
        if (!op)
            op = tryGetOperand(Inst->getOperand(1));

        if (!op)
            return createUnknown(Inst);
    }

    // we don't know what the operation does,
    // so set unknown offset
    node = PS.create(PSNodeType::GEP, op, Offset::UNKNOWN);
    addNode(Inst, node);

    assert(node);
    return node;
}

PSNode *LLVMPointerSubgraphBuilder::createReturn(const llvm::Instruction *Inst)
{
    PSNode *op1 = nullptr;
    // is nullptr if this is 'ret void'
    llvm::Value *retVal = llvm::cast<llvm::ReturnInst>(Inst)->getReturnValue();

    // we create even void and non-pointer return nodes,
    // since these modify CFG (they won't bear any
    // points-to information though)
    // XXX is that needed?

    // DONT: if(retVal->getType()->isPointerTy())
    // we have ptrtoint which break the types...
    if (retVal) {
        // A struct is being returned. In this case,
        // return the address of the local variable
        // that holds the return value, so that
        // we can then do a load on this object
        if (retVal->getType()->isAggregateType()) {
            if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(retVal)) {
                op1 = getOperand(LI->getPointerOperand());
            }

            if (!op1) {
                llvm::errs() << "WARN: Unsupported return of an aggregate type\n";
                llvm::errs() << *Inst << "\n";

                op1 = UNKNOWN_MEMORY;
            }
        }

        if (llvm::isa<llvm::ConstantPointerNull>(retVal)
            || isConstantZero(retVal))
            op1 = NULLPTR;
        else if (typeCanBePointer(DL, retVal->getType()) &&
                  (!isInvalid(retVal->stripPointerCasts(), invalidate_nodes) ||
                   llvm::isa<llvm::ConstantExpr>(retVal) ||
                   llvm::isa<llvm::UndefValue>(retVal)))
            op1 = getOperand(retVal);
    }

    assert((op1 || !retVal || !retVal->getType()->isPointerTy())
           && "Don't have an operand for ReturnInst with pointer");

    PSNode *node = PS.create(PSNodeType::RETURN, op1, nullptr);
    addNode(Inst, node);

    return node;
}

} // namespace pta
} // namespace analysis
} // namespace dg

