/// XXX add licence
//

#ifndef HAVE_LLVM
# error "Need LLVM for LLVMDependenceGraph"
#endif

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"

using llvm::errs;

namespace dg {

LLVMNode::~LLVMNode()
{
    delete memoryobj;
    delete[] operands;
}

LLVMNode **LLVMNode::findOperands()
{
    using namespace llvm;
    const Value *val = getKey();

    if (isa<AllocaInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(val);
        operands_num = 1;
    } else if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        operands = new LLVMNode *[2];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands[1] = dg->getNode(Inst->getValueOperand());
        operands_num = 2;
        assert(operands[0] && "StoreInst pointer operand without node");
        if (!operands[1] && !isa<Constant>(Inst->getValueOperand())) {
            errs() << "WARN: StoreInst value operand without node: "
                   << *Inst->getValueOperand() << "\n";
        }
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands_num = 1;
    } else if (const GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands_num = 1;
    } else if (const CallInst *Inst = dyn_cast<CallInst>(val)) {
        // we store the called function as a first operand
        // and all the arguments as the other operands
        operands_num = Inst->getNumArgOperands() + 1;
        operands = new LLVMNode *[operands_num];
        operands[0] = dg->getNode(Inst->getCalledValue());
        for (unsigned i = 0; i < operands_num - 1; ++i)
            operands[i + 1] = dg->getNode(Inst->getArgOperand(i));
    } else if (const ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getReturnValue());
        operands_num = 1;
    } else if (const CastInst *Inst = dyn_cast<CastInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->stripPointerCasts());
        if (!operands[0])
            errs() << "WARN: CastInst with unstrippable pointer cast" << *Inst << "\n";
        operands_num = 1;
    }

    assert(operands && "findOperands not implemented for this instr");

    return operands;
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph)
{
    using namespace llvm;

    const CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

    // do not add redundant nodes
    const Function *func = CInst->getCalledFunction();
    if (func->arg_size() == 0)
        return;

    LLVMDGParameters *params = new LLVMDGParameters();
    LLVMDGParameters *old = addParameters(params);
    assert(old == nullptr && "Replaced parameters");

    LLVMNode *in, *out;
    for (const Value *val : CInst->arg_operands()) {
        in = new LLVMNode(val);
        out = new LLVMNode(val);
        params->add(val, in, out);

        // add control edges from this node to parameters
        addControlDependence(in);
        addControlDependence(out);

        // add parameter edges -- these are just normal dependece edges
        //LLVMNode *fp = (*funcGraph)[val];
        //assert(fp && "Do not have formal parametr");
        //nn->addDataDependence(fp);
    }
}

LLVMNode **LLVMNode::getOperands()
{
    if (!operands)
        findOperands();

    return operands;
}

size_t LLVMNode::getOperandsNum()
{
    if (!operands)
        findOperands();

    return operands_num;
}

LLVMNode *LLVMNode::getOperand(unsigned int idx)
{
    if (!operands)
        findOperands();

    assert(operands_num > idx && "idx is too high");
    return operands[idx];
}

LLVMNode *LLVMNode::setOperand(LLVMNode *op, unsigned int idx)
{
    assert(operands && "Operands has not been found yet");
    assert(operands_num > idx && "idx is too high");

    LLVMNode *old = operands[idx];
    operands[idx] = op;

    return old;
}

} // namespace dg
