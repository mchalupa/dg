#ifndef _DG_LLVMRDBUILDERSEMISPARSE_H
#define _DG_LLVMRDBUILDERSEMISPARSE_H

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "dg/BBlock.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h"

namespace dg {

enum class AllocationFunction;

namespace analysis {
namespace rd {

using RDBlock = BBlock<RDNode>;

class LLVMRDBuilderSemisparse : public LLVMRDBuilder
{
    // each LLVM block is mapped to multiple RDBlocks.
    // this is necessary for function inlining, where
    std::unordered_map<const llvm::Value *, std::vector<std::unique_ptr<RDBlock>>> blocks;
    // all constructed functions and their corresponding blocks
    std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, std::vector<RDBlock *>>> functions_blocks;

public:
    LLVMRDBuilderSemisparse(const llvm::Module *m,
                  LLVMPointerAnalysis *p,
                  const LLVMReachingDefinitionsAnalysisOptions& opts)
        : LLVMRDBuilder(m, p, opts) {}

    virtual ~LLVMRDBuilderSemisparse() = default;

    RDNode *build() override;

    RDNode *getOperand(const llvm::Value *val, RDBlock *rb);
    RDNode *createNode(const llvm::Instruction& Inst, RDBlock *rb);

    const std::unordered_map<const llvm::Value *, std::vector<std::unique_ptr<RDBlock>>>& getBlocks() const {
        return blocks;
    }

    std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, std::vector<RDBlock *>>>& getConstructedFunctions() {
        return functions_blocks;
    }

private:
    void addNode(const llvm::Value *val, RDNode *node)
    {
        auto it = nodes_map.find(val);
        assert(it == nodes_map.end() && "Adding a node that we already have");

        nodes_map.emplace_hint(it, val, node);
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    void addBlock(const llvm::Value *val, RDBlock *block) {
        block->setKey(const_cast<llvm::Value *>(val));
        blocks[val].push_back(std::unique_ptr<RDBlock>(block));
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

    std::vector<DefSite> getPointsTo(const llvm::Value *val, RDBlock *rb);
    bool isStrongUpdate(const llvm::Value *val, const DefSite& ds, RDBlock *rb);

    RDNode *createLoad(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createStore(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createAlloc(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createDynAlloc(const llvm::Instruction *Inst,
                           AllocationFunction type, RDBlock *rb);
    RDNode *createRealloc(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createReturn(const llvm::Instruction *Inst, RDBlock *rb);

    std::vector<RDBlock *> buildBlock(const llvm::BasicBlock& block);
    std::pair<RDBlock *,RDBlock *> buildFunction(const llvm::Function& F);

    RDBlock *buildGlobals();

    std::pair<RDNode *, RDNode *>
    createCallToFunction(const llvm::Function *F, RDBlock *rb);

    std::pair<RDNode *, RDNode *>
    createCall(const llvm::Instruction *Inst, RDBlock *rb);

    RDNode *createIntrinsicCall(const llvm::CallInst *CInst, RDBlock *rb);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst, RDBlock *rb);
};

} // namespace rd
} // namespace dg
} // namespace analysis

#endif /* _DG_LLVMRDBUILDERSEMISPARSE_H */
