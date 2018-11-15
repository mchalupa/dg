#ifndef _LLVM_DG_RD_DENSE_H_
#define _LLVM_DG_RD_DENSE_H_

#include <vector>
#include <unordered_map>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h"

namespace dg {
namespace analysis {
namespace rd {

class LLVMRDBuilderDense : public LLVMRDBuilder {
public:
    LLVMRDBuilderDense(const llvm::Module *m,
                  dg::LLVMPointerAnalysis *p,
                  const LLVMReachingDefinitionsAnalysisOptions& opts)
        : LLVMRDBuilder(m, p, opts) {}
    virtual ~LLVMRDBuilderDense() = default;

    RDNode *build() override;

    RDNode *getOperand(const llvm::Value *val);
    RDNode *createNode(const llvm::Instruction& Inst);

private:
    enum class CallType{CREATE_THREAD, JOIN_THREAD, PLAIN_CALL};

    struct FunctionCall {
        FunctionCall(RDNode *rootNode, RDNode *returnNode, CallType callType);
        RDNode *rootNode;
        RDNode *returnNode;
        CallType callType;
    };

    void addNode(const llvm::Value *val, RDNode *node)
    {
        auto it = nodes_map.find(val);
        assert(it == nodes_map.end() && "Adding a node that we already have");

        nodes_map.emplace_hint(it, val, node);
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    ///
    // Add a dummy node for which there's no real LLVM node
    void addNode(RDNode *node)
    {
        dummy_nodes.push_back(node);
    }

    void addMapping(const llvm::Value *val, RDNode *node)
    {
        auto it = mapping.find(val);
        assert(it == mapping.end() && "Adding mapping that we already have");

        mapping.emplace_hint(it, val, node);
    }

    RDNode *createStore(const llvm::Instruction *Inst);
    RDNode *createAlloc(const llvm::Instruction *Inst);
    RDNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RDNode *createRealloc(const llvm::Instruction *Inst);
    RDNode *createReturn(const llvm::Instruction *Inst);

    std::pair<RDNode *, RDNode *> buildBlock(const llvm::BasicBlock& block);
    std::pair<RDNode *, RDNode *> buildFunction(const llvm::Function& F);

    std::pair<RDNode *, RDNode *> buildGlobals();

    FunctionCall
    createCallToFunction(const llvm::Function *F);

    std::vector<FunctionCall>
    createCall(const llvm::Instruction *Inst);

    std::vector<FunctionCall>
    createCallsToZeroSizeFunctions(const llvm::Function *function,
                                     const llvm::CallInst *CInst);

    std::vector<FunctionCall>
    createCallsToFunctions(const std::vector<const llvm::Function *> &functions,
                           const llvm::CallInst *CInst);

    std::vector<FunctionCall>
    createPthreadCreateCalls(const llvm::CallInst *CInst);

    FunctionCall createPthreadJoinCall(const llvm::CallInst *CInst);

    RDNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst);

    std::vector<const llvm::Function *>
    getPointsToFunctions(const llvm::Value *calledValue);

    std::vector<const llvm::Function *>
    getPotentialFunctions(const llvm::Instruction *instruction);

    bool isInlineAsm(const llvm::Instruction *instruction);

    const llvm::Function *
    findFunctionAndRemoveFromVector(std::vector<const llvm::Function *> &functions,
                                    const std::__cxx11::string &functionName);

    void matchForksAndJoins();

    void connectCallsToGraph(const llvm::Instruction *Inst,
                             const std::vector<FunctionCall> &functionCalls,
                             RDNode *&lastNode);
};

}
}
}

#endif // _LLVM_DG_RD_DENSE_H_

