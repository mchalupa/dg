#ifndef _LLVM_DG_RD_DENSE_H_
#define _LLVM_DG_RD_DENSE_H_

#include <vector>
#include <unordered_map>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h"

namespace dg {
namespace analysis {
namespace rd {

class LLVMRDBuilderDense : public LLVMRDBuilder {
public:
    LLVMRDBuilderDense(const llvm::Module *m,
                       dg::LLVMPointerAnalysis *p,
                       const LLVMReachingDefinitionsAnalysisOptions& opts,
                       bool buildUses = false)
        : LLVMRDBuilder(m, p, opts), buildUses(buildUses) {}
    virtual ~LLVMRDBuilderDense() = default;

    ReachingDefinitionsGraph build() override;

    RDNode *getOperand(const llvm::Value *val);
    RDNode *createNode(const llvm::Instruction& Inst);

private:
    std::vector<DefSite> mapPointers(const llvm::Value *where,
                                     const llvm::Value *val,
                                     Offset size);

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

    void addArtificialNode(const llvm::Value *val, RDNode *node)
    {
        node->setUserData(const_cast<llvm::Value *>(val));
        dummy_nodes.push_back(node);
    }

    void addMapping(const llvm::Value *val, RDNode *node)
    {
        auto it = mapping.find(val);
        assert(it == mapping.end() && "Adding mapping that we already have");

        mapping.emplace_hint(it, val, node);
    }

    RDNode *createStore(const llvm::Instruction *Inst);
    RDNode *createLoad(const llvm::Instruction *Inst);
    RDNode *createAlloc(const llvm::Instruction *Inst);
    RDNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RDNode *createRealloc(const llvm::Instruction *Inst);
    RDNode *createReturn(const llvm::Instruction *Inst);


    RDNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);
    std::pair<RDNode *, RDNode *> buildBlock(const llvm::BasicBlock& block);
    std::pair<RDNode *, RDNode *> buildFunction(const llvm::Function& F);

    std::pair<RDNode *, RDNode *> buildGlobals();

    std::pair<RDNode *, RDNode *> createCallToFunction(const llvm::Function *F, const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createCall(const llvm::Instruction *Inst);

    std::pair<RDNode *, RDNode *> createCallToZeroSizeFunction(const llvm::Function *function,
                                     const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                           const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createPthreadCreateCalls(const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createPthreadJoinCall(const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createPthreadExitCall(const llvm::CallInst *CInst);
    RDNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst);

    bool buildUses{false};

    bool isInlineAsm(const llvm::Instruction *instruction);

    void matchForksAndJoins();
};

}
}
}

#endif // _LLVM_DG_RD_DENSE_H_

