#ifndef _LLVM_DG_RD_BUILDER_H
#define _LLVM_DG_RD_BUILDER_H

#include <unordered_map>
#include <memory>

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

#include "dg/analysis/ReadWriteGraph/ReadWriteGraph.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

namespace dg {
namespace analysis {

class LLVMRDBuilderBase {
protected:
    const llvm::Module *M;
    const llvm::DataLayout *DL;
    const LLVMReachingDefinitionsAnalysisOptions& _options;

    ReachingDefinitionsGraph graph;

    struct Block {
        std::vector<RDNode *> nodes;
    };

    struct Subgraph {
        std::map<const llvm::BasicBlock *, Block> blocks;

        Block& createBlock(const llvm::BasicBlock *b) {
            auto it = blocks.emplace(b, Block());
            assert(it.second && "Already had this block");

            return it.first->second;
        }
        Block *entry{nullptr};
        std::vector<RDNode *> returns;
    };

    // points-to information
    dg::LLVMPointerAnalysis *PTA;

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, RDNode *> nodes_map;

    std::map<const llvm::CallInst *, RDNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RDNode *> threadJoinCalls;

    // mapping of call nodes to called subgraphs
    std::map<std::pair<RDNode *, RDNode *>, std::set<Subgraph *>> calls;

    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;

    RDNode *create(RDNodeType t) { return graph.create(t); }

public:
    LLVMRDBuilderBase(const llvm::Module *m,
                      dg::LLVMPointerAnalysis *p,
                      const LLVMReachingDefinitionsAnalysisOptions& opts)
        : M(m), DL(new llvm::DataLayout(m)), _options(opts), PTA(p) {}

    virtual ~LLVMRDBuilderBase() {
        // delete data layout
        delete DL;
    }

    virtual ReachingDefinitionsGraph&& build() = 0;

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RDNode *>&
                                getNodesMap() const { return nodes_map; }

    RDNode *getNode(const llvm::Value *val) {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }
};

class LLVMRDBuilder : public LLVMRDBuilderBase {
public:
    LLVMRDBuilder(const llvm::Module *m,
                  dg::LLVMPointerAnalysis *p,
                  const LLVMReachingDefinitionsAnalysisOptions& opts,
                  bool forget_locals = false)
        : LLVMRDBuilderBase(m, p, opts),
          buildUses(true), forgetLocalsAtReturn(forget_locals) {}
    virtual ~LLVMRDBuilder() = default;

    ReachingDefinitionsGraph&& build() override;

    RDNode *getOperand(const llvm::Value *val);

private:

    static void blockAddSuccessors(Subgraph& subg, Block& block,
                                   const llvm::BasicBlock *llvmBlock,
                                   std::set<const llvm::BasicBlock *>& visited);

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

    // FIXME: rename this method
    void addArtificialNode(const llvm::Value *val, RDNode *node)
    {
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    RDNode *createStore(const llvm::Instruction *Inst);
    RDNode *createLoad(const llvm::Instruction *Inst);
    RDNode *createAlloc(const llvm::Instruction *Inst);
    RDNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RDNode *createRealloc(const llvm::Instruction *Inst);
    RDNode *createReturn(const llvm::Instruction *Inst);


    RDNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);
    Block& buildBlock(Subgraph& subg, const llvm::BasicBlock& block);
    Block& buildBlockNodes(Subgraph& subg, const llvm::BasicBlock& block);
    Subgraph& buildFunction(const llvm::Function& F);
    Subgraph *getOrCreateSubgraph(const llvm::Function *F);

    std::pair<RDNode *, RDNode *> buildGlobals();

    std::pair<RDNode *, RDNode *> createCallToFunction(const llvm::Function *F, const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createCall(const llvm::Instruction *Inst);

    RDNode * createCallToZeroSizeFunction(const llvm::Function *function,
                                         const llvm::CallInst *CInst);

    std::pair<RDNode *, RDNode *> createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                           const llvm::CallInst *CInst);

    RDNode * createPthreadCreateCalls(const llvm::CallInst *CInst);
    RDNode * createPthreadJoinCall(const llvm::CallInst *CInst);
    RDNode * createPthreadExitCall(const llvm::CallInst *CInst);

    RDNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst);

    // even the data-flow analysis needs uses to have the mapping of llvm values
    bool buildUses{true};
    bool forgetLocalsAtReturn{false};

    bool isInlineAsm(const llvm::Instruction *instruction);

    void matchForksAndJoins();
};

} // namespace analysis
} // namespace dg

#endif // _LLVM_DG_RD_BUILDER_H
