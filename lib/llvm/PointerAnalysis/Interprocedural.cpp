#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/Support/CFG.h>
#else
#include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include "dg/llvm/PointerAnalysis/PointerGraph.h"

#include "llvm/llvm-utils.h"

namespace dg {
namespace pta {

void LLVMPointerGraphBuilder::addArgumentOperands(const llvm::CallInst *CI,
                                                  PSNode *arg, unsigned idx) {
    assert(idx < llvmutils::getNumArgOperands(CI));
    PSNode *op = tryGetOperand(CI->getArgOperand(idx));
    if (op && !arg->hasOperand(op)) {
        // NOTE: do not add an operand multiple-times
        // (when a function is called multiple-times with
        // the same actual parameters)
        arg->addOperand(op);
    }
}

void LLVMPointerGraphBuilder::addArgumentOperands(const llvm::CallInst &CI,
                                                  PSNode &node) {
    for (const auto &arg : llvmutils::args(CI)) {
        PSNode *operand = tryGetOperand(arg);
        if (operand && !node.hasOperand(operand)) {
            node.addOperand(operand);
        }
    }
}

void LLVMPointerGraphBuilder::addArgumentOperands(const llvm::Function *F,
                                                  PSNode *arg, unsigned idx) {
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addArgumentOperands(CI, arg, idx);
    }
}

void LLVMPointerGraphBuilder::addArgumentsOperands(const llvm::Function *F,
                                                   const llvm::CallInst *CI,
                                                   unsigned index) {
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++index) {
        auto it = nodes_map.find(&*A);
        assert(it != nodes_map.end());
        PSNodesSeq &cur = it->second;

        if (CI) {
            // with func ptr call we know from which
            // call we should take the values
            addArgumentOperands(CI, cur.getSingleNode(), index);
        } else {
            // with regular call just use all calls
            addArgumentOperands(F, cur.getSingleNode(), index);
        }
    }
}

void LLVMPointerGraphBuilder::addVariadicArgumentOperands(
        const llvm::Function *F, const llvm::CallInst *CI, PSNode *arg) {
    for (unsigned idx = F->arg_size() - 1;
         idx < llvmutils::getNumArgOperands(CI); ++idx)
        addArgumentOperands(CI, arg, idx);
}

void LLVMPointerGraphBuilder::addVariadicArgumentOperands(
        const llvm::Function *F, PSNode *arg) {
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addVariadicArgumentOperands(F, CI, arg);
        // if this is funcptr, we handle it in the other
        // version of addVariadicArgumentOperands
    }
}

void LLVMPointerGraphBuilder::addReturnNodesOperands(const llvm::Function *F,
                                                     PointerSubgraph &subg,
                                                     PSNode *callNode) {
    using namespace llvm;

    for (PSNode *r : subg.returnNodes) {
        // call-return node is like a PHI node
        // But we're interested only in the nodes that return some value
        // from subprocedure, not for all nodes that have no successor.
        if (callNode) {
            addReturnNodeOperand(callNode, r);
        } else {
            addReturnNodeOperand(F, r);
        }
    }
}

void LLVMPointerGraphBuilder::addReturnNodeOperand(PSNode *callNode,
                                                   PSNode *ret) {
    assert(PSNodeRet::get(ret));
    auto *callReturn = PSNodeCallRet::cast(callNode->getPairedNode());
    // the function must be defined, since we have the return node,
    // so there must be associated the return node
    assert(callReturn);
    assert(callReturn != callNode);
    assert(callReturn->getType() == PSNodeType::CALL_RETURN);

    if (!callReturn->hasOperand(ret))
        callReturn->addOperand(ret);

    // setup return edges (do it here, since recursive calls
    // may not have build return nodes earlier)
    PSNodeRet::get(ret)->addReturnSite(callReturn);
    callReturn->addReturn(ret);
}

void LLVMPointerGraphBuilder::addReturnNodeOperand(const llvm::Function *F,
                                                   PSNode *op) {
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        // get every call and its assocciated return and add the operand
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F) {
            auto *nodes = getNodes(CI);
            // since we're building the graph only for the reachable nodes from
            // the entry, we may not have all call-sites of this function
            if (!nodes)
                continue;
            PSNode *callNode = nodes->getFirst();
            assert(PSNodeCall::cast(callNode) && "Got wrong node");
            addReturnNodeOperand(callNode, op);
        }
    }
}

void LLVMPointerGraphBuilder::addInterproceduralPthreadOperands(
        const llvm::Function *F, const llvm::CallInst *CI) {
    // last argument (with index 3) is argument to function pthread_create will
    // call
    addArgumentsOperands(F, CI, 3);
}

void LLVMPointerGraphBuilder::addInterproceduralOperands(
        const llvm::Function *F, PointerSubgraph &subg,
        const llvm::CallInst *CI, PSNode *callNode) {
    assert((!CI || callNode) && (!callNode || CI));

    // add operands to arguments' PHI nodes
    addArgumentsOperands(F, CI);

    if (F->isVarArg()) {
        assert(subg.vararg);
        if (CI)
            // funcptr call
            addVariadicArgumentOperands(F, CI, subg.vararg);
        else
            addVariadicArgumentOperands(F, subg.vararg);
    }

    if (!subg.returnNodes.empty()) {
        addReturnNodesOperands(F, subg, callNode);
    } else if (callNode) {
        // disconnect call-return nodes
        auto *callReturnNode = PSNodeCallRet::cast(callNode->getPairedNode());
        assert(callReturnNode && callNode != callReturnNode);
        (void) callReturnNode; // c++17 TODO: replace with [[maybe_unused]]

        if (callNode->successorsNum() != 0) {
            assert(callNode->getSingleSuccessor() == callReturnNode);
            callNode->removeSingleSuccessor();
        } else {
            // the call does not return
            assert(callNode->successorsNum() == 0);
        }
    }
}

} // namespace pta
} // namespace dg
