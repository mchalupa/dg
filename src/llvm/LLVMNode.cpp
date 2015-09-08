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
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"

using llvm::errs;

namespace dg {

LLVMNode::~LLVMNode()
{
    if (owns_key)
        delete getKey();

    delete memoryobj;
    delete[] operands;
}

void LLVMNode::dump() const
{
    getKey()->dump();
}

LLVMNode **LLVMNode::findOperands()
{
    using namespace llvm;
    const Value *val = getKey();

    // FIXME use op_begin() and op_end() and do it generic
    // for all the instructions (+ maybe some speacial handling
    // like CallInst?)

    // we have Function nodes stored in globals
    if (isa<AllocaInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(val);
        operands_num = 1;
    } else if (const StoreInst *Inst = dyn_cast<StoreInst>(val)) {
        operands = new LLVMNode *[2];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands[1] = dg->getNode(Inst->getValueOperand());
#ifdef DEBUG_ENABLED
        if (!operands[0] && !isa<Constant>(Inst->getPointerOperand()))
            errs() << "WARN: Didn't found pointer for " << *Inst << "\n";
        if (!operands[1] && !isa<Constant>(Inst->getValueOperand()))
            errs() << "WARN: Didn't found value for " << *Inst << "\n";
#endif
        operands_num = 2;
    } else if (const LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        operands = new LLVMNode *[1];
        const Value *op = Inst->getPointerOperand();
        operands[0] = dg->getNode(op);
#ifdef DEBUG_ENABLED
        if (!operands[0])
            errs() << "WARN: Didn't found pointer for " << *Inst << "\n";
#endif
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

    if (!operands) {
        errs() << "Unhandled instruction: " << *val
               << "Type: " << *val->getType() << "\n";
        abort();
    }

    return operands;
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph)
{
    using namespace llvm;

    const CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

    // do not add redundant nodes
    const Function *func = CInst->getCalledFunction();
    if (!func || func->arg_size() == 0)
        return;

    addActualParameters(funcGraph, func);
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph,
                                   const llvm::Function *func)
{
    using namespace llvm;

    const CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

    // do not do nothing if we have parameters.
    // This is probably adding subgraph due to function
    // pointer in data-flow analysis
    if (getParameters())
        return;

    LLVMDGParameters *formal = funcGraph->getParameters();
    LLVMDGParameters *params = new LLVMDGParameters();
    LLVMDGParameters *old = addParameters(params);
    assert(old == nullptr && "Replaced parameters");

    // we need to add edges from actual parameters to formal parameteres
    // so if the params exists, just add the edges. If the params
    // do not exists, create them and then add the edges

    LLVMNode *in, *out;
    int idx = 0;
    for (auto A = func->arg_begin(), E = func->arg_end();
         A != E; ++A, ++idx) {
        const Value *opval = CInst->getArgOperand(idx);
        LLVMDGParameter *fp = formal->find(&*A);
        if (!fp) {
            errs() << "ERR: no formal param for value: " << *opval << "\n";
            continue;
        }

        in = new LLVMNode(opval);
        out = new LLVMNode(opval);
        params->add(opval, in, out, fp);

        // add control edges from the call-site node
        // to the parameters
        addControlDependence(in);
        addControlDependence(out);

        // from actual in to formal in
        in->addDataDependence(fp->in);
        // from formal out to actual out
        fp->out->addDataDependence(out);
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
