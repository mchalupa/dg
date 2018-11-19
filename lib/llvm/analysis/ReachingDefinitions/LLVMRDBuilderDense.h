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


    RDNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);
    std::pair<RDNode *, RDNode *> buildBlock(const llvm::BasicBlock& block);
    std::pair<RDNode *, RDNode *> buildFunction(const llvm::Function& F);

    std::pair<RDNode *, RDNode *> buildGlobals();

    std::pair<RDNode *, RDNode *>
    createCallToFunction(const llvm::Function *F);

    std::pair<RDNode *, RDNode *>
    createCall(const llvm::Instruction *Inst);

    RDNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst);
};

}
}
}

#endif // _LLVM_DG_RD_DENSE_H_

