#ifndef LLVM_DG_RWG_BUILDER_H
#define LLVM_DG_RWG_BUILDER_H

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

#include "dg/ReadWriteGraph/ReadWriteGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"

namespace dg {
namespace dda {

class LLVMReadWriteGraphBuilder {
    const LLVMDataDependenceAnalysisOptions& _options;
    const llvm::Module *M;
    // points-to information
    dg::LLVMPointerAnalysis *PTA;

    ReadWriteGraph graph;

    struct Block {
        std::vector<RWNode *> nodes;
    };

    struct Subgraph {
        std::map<const llvm::BasicBlock *, Block> blocks;

        Block& createBlock(const llvm::BasicBlock *b) {
            auto it = blocks.emplace(b, Block());
            assert(it.second && "Already had this block");

            return it.first->second;
        }
        Block *entry{nullptr};
        std::vector<RWNode *> returns;

        RWSubgraph *rwsubgraph;
    };

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, RWNode *> nodes_map;

    std::map<const llvm::CallInst *, RWNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RWNode *> threadJoinCalls;

    // mapping of call nodes to called subgraphs
    std::map<std::pair<RWNode *, RWNode *>, std::set<Subgraph *>> calls;

    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;

    RWNode *create(RWNodeType t) { return graph.create(t); }

public:
    LLVMReadWriteGraphBuilder(const llvm::Module *m,
                              dg::LLVMPointerAnalysis *p,
                              const LLVMDataDependenceAnalysisOptions& opts,
                              bool forget_locals = false)
        : _options(opts), M(m), PTA(p),
          buildUses(true), forgetLocalsAtReturn(forget_locals) {}

    ReadWriteGraph&& build();

    RWNode *getOperand(const llvm::Value *val);

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RWNode *>&
                                getNodesMap() const { return nodes_map; }

    RWNode *getNode(const llvm::Value *val) {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }


private:

    static void blockAddSuccessors(Subgraph& subg, Block& block,
                                   const llvm::BasicBlock *llvmBlock,
                                   std::set<const llvm::BasicBlock *>& visited);

    std::vector<DefSite> mapPointers(const llvm::Value *where,
                                     const llvm::Value *val,
                                     Offset size);

    void addNode(const llvm::Value *val, RWNode *node)
    {
        auto it = nodes_map.find(val);
        assert(it == nodes_map.end() && "Adding a node that we already have");

        nodes_map.emplace_hint(it, val, node);
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    // FIXME: rename this method
    void addArtificialNode(const llvm::Value *val, RWNode *node)
    {
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    RWNode *createStore(const llvm::Instruction *Inst);
    RWNode *createLoad(const llvm::Instruction *Inst);
    RWNode *createAlloc(const llvm::Instruction *Inst);
    RWNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RWNode *createRealloc(const llvm::Instruction *Inst);
    RWNode *createReturn(const llvm::Instruction *Inst);


    RWNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);
    Block& buildBlock(Subgraph& subg, const llvm::BasicBlock& block);
    Block& buildBlockNodes(Subgraph& subg, const llvm::BasicBlock& block);
    Subgraph& buildFunction(const llvm::Function& F);
    Subgraph *getOrCreateSubgraph(const llvm::Function *F);

    std::pair<RWNode *, RWNode *> buildGlobals();

    std::pair<RWNode *, RWNode *> createCallToFunction(const llvm::Function *F, const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *> createCall(const llvm::Instruction *Inst);

    RWNode * createCallToZeroSizeFunction(const llvm::Function *function,
                                         const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *> createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                           const llvm::CallInst *CInst);

    RWNode * createPthreadCreateCalls(const llvm::CallInst *CInst);
    RWNode * createPthreadJoinCall(const llvm::CallInst *CInst);
    RWNode * createPthreadExitCall(const llvm::CallInst *CInst);

    RWNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RWNode *createUndefinedCall(const llvm::CallInst *CInst);

    // even the data-flow analysis needs uses to have the mapping of llvm values
    bool buildUses{true};
    bool forgetLocalsAtReturn{false};

    bool isInlineAsm(const llvm::Instruction *instruction);

    void matchForksAndJoins();
};

} // namespace dda
} // namespace dg

#endif
