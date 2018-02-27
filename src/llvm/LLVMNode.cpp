#ifndef HAVE_LLVM
# error "Need LLVM for LLVMDependenceGraph"
#endif

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/raw_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "LLVMNode.h"
#include "LLVMDependenceGraph.h"

using llvm::errs;

namespace dg {

LLVMNode::~LLVMNode()
{
    delete[] operands;
}

LLVMNode **LLVMNode::findOperands()
{
    using namespace llvm;
    Value *val = getKey();

    // FIXME use op_begin() and op_end() and do it generic
    // for all the instructions (+ maybe some speacial handling
    // like CallInst?)

    // we have Function nodes stored in globals
    if (isa<AllocaInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(val);
        operands_num = 1;
    } else if (StoreInst *Inst = dyn_cast<StoreInst>(val)) {
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
    } else if (LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        operands = new LLVMNode *[1];
        Value *op = Inst->getPointerOperand();
        operands[0] = dg->getNode(op);
#ifdef DEBUG_ENABLED
        if (!operands[0] && !isa<ConstantExpr>(Inst->getPointerOperand()))
            errs() << "WARN: Didn't found pointer for " << *Inst << "\n";
#endif
        operands_num = 1;
    } else if (GetElementPtrInst *Inst = dyn_cast<GetElementPtrInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getPointerOperand());
        operands_num = 1;
    } else if (CallInst *Inst = dyn_cast<CallInst>(val)) {
        // we store the called function as a first operand
        // and all the arguments as the other operands
        operands_num = Inst->getNumArgOperands() + 1;
        operands = new LLVMNode *[operands_num];
        operands[0] = dg->getNode(Inst->getCalledValue());
        for (unsigned i = 0; i < operands_num - 1; ++i)
            operands[i + 1] = dg->getNode(Inst->getArgOperand(i));
    } else if (ReturnInst *Inst = dyn_cast<ReturnInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->getReturnValue());
        operands_num = 1;
    } else if (CastInst *Inst = dyn_cast<CastInst>(val)) {
        operands = new LLVMNode *[1];
        operands[0] = dg->getNode(Inst->stripPointerCasts());
        if (!operands[0])
            errs() << "WARN: CastInst with unstrippable pointer cast" << *Inst << "\n";
        operands_num = 1;
    } else if (PHINode *Inst = dyn_cast<PHINode>(val)) {
        operands_num = Inst->getNumIncomingValues();
        operands = new LLVMNode *[operands_num];
        for (unsigned n = 0; n < operands_num; ++n) {
            operands[n] = dg->getNode(Inst->getIncomingValue(n));
        }
    } else if (SelectInst *Inst = dyn_cast<SelectInst>(val)) {
        operands_num = 2;
        operands = new LLVMNode *[operands_num];
        for (unsigned n = 0; n < operands_num; ++n) {
            operands[n] = dg->getNode(Inst->getOperand(n + 1));
        }
    }


    if (!operands) {
        errs() << "Unhandled instruction: " << *val
               << "Type: " << *val->getType() << "\n";
        abort();
    }

    return operands;
}

static void addGlobalsParams(LLVMDGParameters *params, LLVMNode *callNode, LLVMDependenceGraph *funcGraph)
{
    LLVMDGParameters *formal = funcGraph->getParameters();
    if (!formal)
        return;

    LLVMNode *pin, *pout;
    for (auto I = formal->global_begin(), E = formal->global_end(); I != E; ++I) {
        LLVMDGParameter& p = I->second;
        llvm::Value *val = I->first;
        LLVMDGParameter *act = params->findGlobal(val);
        // reuse or create the parameter
        if (!act) {
            pin = new LLVMNode(val);
            pout = new LLVMNode(val);
            pin->setDG(callNode->getDG());
            pout->setDG(callNode->getDG());
            params->addGlobal(val, pin, pout);
        } else {
            pin = act->in;
            pout = act->out;
        }

        // connect the globals with edges
        pin->addDataDependence(p.in);
        p.out->addDataDependence(pout);

        // add control dependence from call node
        callNode->addControlDependence(pin);
        callNode->addControlDependence(pout);
    }
}

static void addDynMemoryParams(LLVMDGParameters *params, LLVMNode *callNode, LLVMDependenceGraph *funcGraph)
{
    LLVMDGParameters *formal = funcGraph->getParameters();
    if (!formal)
        return;

    LLVMNode *pin, *pout;
    for (auto& it : *formal) {
        // dyn. memory params are only callinsts
        if (!llvm::isa<llvm::CallInst>(it.first))
            continue;

        LLVMDGParameter& p = it.second;
        llvm::Value *val = it.first;
        LLVMDGParameter *act = params->find(val);

        // reuse or create the parameter
        if (!act) {
            pin = new LLVMNode(val);
            pout = new LLVMNode(val);
            pin->setDG(callNode->getDG());
            pout->setDG(callNode->getDG());
            params->add(val, pin, pout);
        } else {
            pin = act->in;
            pout = act->out;
        }

        // connect the params with edges
        pin->addDataDependence(p.in);
        p.out->addDataDependence(pout);

        // add control dependence from call node
        callNode->addControlDependence(pin);
        callNode->addControlDependence(pout);
    }
}



static void addOperandsParams(LLVMDGParameters *params,
                              LLVMDGParameters *formal,
                              LLVMNode *callNode,
                              llvm::Function *func)
{

    llvm::CallInst *CInst
        = llvm::dyn_cast<llvm::CallInst>(callNode->getValue());
    assert(CInst && "addActualParameters called on non-CallInst");

    LLVMNode *in, *out;
    int idx = 0;
    for (auto A = func->arg_begin(), E = func->arg_end();
         A != E; ++A, ++idx) {
        llvm::Value *opval = CInst->getArgOperand(idx);
        LLVMDGParameter *fp = formal->find(&*A);
        if (!fp) {
            errs() << "ERR: no formal param for value: " << *opval << "\n";
            continue;
        }

        LLVMDGParameter *ap = params->find(opval);
        if (!ap) {
            in = new LLVMNode(opval);
            out = new LLVMNode(opval);
            in->setDG(callNode->getDG());
            out->setDG(callNode->getDG());
            params->add(opval, in, out);
        } else {
            in = ap->in;
            out = ap->out;
        }

        // add control edges from the call-site node
        // to the parameters
        callNode->addControlDependence(in);
        callNode->addControlDependence(out);

        // from actual in to formal in
        in->addDataDependence(fp->in);
        // from formal out to actual out
        fp->out->addDataDependence(out);
    }
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph)
{
    using namespace llvm;

    CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

    // do not add redundant nodes
    Function *func
        = dyn_cast<Function>(CInst->getCalledValue()->stripPointerCasts());
    if (!func || func->size() == 0)
        return;

    addActualParameters(funcGraph, func);
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph,
                                   llvm::Function *func)
{
    using namespace llvm;

    LLVMDGParameters *formal = funcGraph->getParameters();
    if (!formal)
        return;

    // if we have parameters, then use them and just
    // add edges to formal parameters (a call-site can
    // have more destinations if it is via function pointer)
    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters(this);
#ifndef NDEBUG
        LLVMDGParameters *old =
#endif
        setParameters(params);
        assert(old == nullptr && "Replaced parameters");
    }

    if (func->arg_size() != 0)
        addOperandsParams(params, formal, this, func);

    // hopefully we matched all the operands, so let's
    // add the global variables the function uses
    addGlobalsParams(params, this, funcGraph);
    addDynMemoryParams(params, this, funcGraph);
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
