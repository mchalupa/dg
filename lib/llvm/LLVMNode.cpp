#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"

using llvm::errs;

namespace dg {

static void addGlobalsParams(LLVMDGParameters *params, LLVMNode *callNode,
                             LLVMDependenceGraph *funcGraph) {
    LLVMDGParameters *formal = funcGraph->getParameters();
    if (!formal)
        return;

    LLVMNode *pin, *pout;
    for (auto I = formal->global_begin(), E = formal->global_end(); I != E;
         ++I) {
        LLVMDGParameter &p = I->second;
        llvm::Value *val = I->first;
        LLVMDGParameter *act = params->findGlobal(val);
        // reuse or create the parameter
        if (!act) {
            std::tie(pin, pout) =
                    params->constructGlobal(val, val, callNode->getDG());
        } else {
            pin = act->in;
            pout = act->out;
        }
        assert(pin && pout);

        // connect the globals with edges
        pin->addDataDependence(p.in);
        p.out->addDataDependence(pout);

        // add control dependence from call node
        callNode->addControlDependence(pin);
        callNode->addControlDependence(pout);
    }
}

static void addDynMemoryParams(LLVMDGParameters *params, LLVMNode *callNode,
                               LLVMDependenceGraph *funcGraph) {
    LLVMDGParameters *formal = funcGraph->getParameters();
    if (!formal)
        return;

    LLVMNode *pin, *pout;
    for (auto &it : *formal) {
        // dyn. memory params are only callinsts
        if (!llvm::isa<llvm::CallInst>(it.first))
            continue;

        LLVMDGParameter &p = it.second;
        llvm::Value *val = it.first;
        LLVMDGParameter *act = params->find(val);

        // reuse or create the parameter
        if (!act) {
            std::tie(pin, pout) =
                    params->construct(val, val, callNode->getDG());
        } else {
            pin = act->in;
            pout = act->out;
        }
        assert(pin && pout);

        // connect the params with edges
        pin->addDataDependence(p.in);
        p.out->addDataDependence(pout);

        // add control dependence from call node
        callNode->addControlDependence(pin);
        callNode->addControlDependence(pout);
    }
}

static void addOperandsParams(LLVMDGParameters *params,
                              LLVMDGParameters *formal, LLVMNode *callNode,
                              llvm::Function *func, bool fork = false) {
    llvm::CallInst *CInst =
            llvm::dyn_cast<llvm::CallInst>(callNode->getValue());
    assert(CInst && "addActualParameters called on non-CallInst");

    LLVMNode *in, *out;
    int idx;
    if (fork) {
        idx = 3;
    } else {
        idx = 0;
    }
    for (auto A = func->arg_begin(), E = func->arg_end(); A != E; ++A, ++idx) {
        llvm::Value *opval = CInst->getArgOperand(idx);
        LLVMDGParameter *fp = formal->find(&*A);
        if (!fp) {
            errs() << "ERR: no formal param for value: " << *opval << "\n";
            continue;
        }

        LLVMDGParameter *ap = params->find(opval);
        if (!ap) {
            std::tie(in, out) =
                    params->construct(opval, opval, callNode->getDG());
        } else {
            in = ap->in;
            out = ap->out;
        }
        assert(in && out);

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

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph) {
    using namespace llvm;

    CallInst *CInst = dyn_cast<CallInst>(key);
    assert(CInst && "addActualParameters called on non-CallInst");

#if LLVM_VERSION_MAJOR >= 8
    Value *val = CInst->getCalledOperand();
#else
    Value *val = CInst->getCalledValue();
#endif
    // do not add redundant nodes
    Function *func = dyn_cast<Function>(val->stripPointerCasts());
    if (!func || func->empty())
        return;

    addActualParameters(funcGraph, func);
}

void LLVMNode::addActualParameters(LLVMDependenceGraph *funcGraph,
                                   llvm::Function *func, bool fork) {
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
        addOperandsParams(params, formal, this, func, fork);

    // hopefully we matched all the operands, so let's
    // add the global variables the function uses
    addGlobalsParams(params, this, funcGraph);
    addDynMemoryParams(params, this, funcGraph);
}

} // namespace dg
