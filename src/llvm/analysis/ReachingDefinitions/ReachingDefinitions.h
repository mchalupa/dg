#ifndef _LLVM_DG_RD_H_
#define _LLVM_DG_RD_H_

#include <unordered_map>
#include <memory>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "llvm/MemAllocationFuncs.h"
#include "BBlock.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/Ssa/SparseRDGraphBuilder.h"
#include "analysis/ReachingDefinitions/SemisparseRda.h"
#include "llvm/analysis/Dominators.h"
#include "llvm/analysis/PointsTo/PointsTo.h"

namespace dg {
namespace analysis {
namespace rd {

using RDBlock = BBlock<RDNode>;

class LLVMRDBuilder
{
    const llvm::Module *M;
    const llvm::DataLayout *DL;
    bool assume_pure_functions;

    struct Subgraph {
        Subgraph(RDNode *r1, RDNode *r2)
            : root(r1), ret(r2) {}
        Subgraph(): root(nullptr), ret(nullptr) {}

        RDNode *root;
        RDNode *ret;
    };

    // points-to information
    dg::LLVMPointerAnalysis *PTA;

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, RDNode *> nodes_map;

    // mapping of llvm nodes to relevant reaching definitions nodes
    // (this is a super-set of nodes_map)
    // we could keep just one map of these two and don't duplicate
    // the information, but this way it is more bug-proof
    std::unordered_map<const llvm::Value *, RDNode *> mapping;

    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;
    // list of dummy nodes (used just to keep the track of memory,
    // so that we can delete it later)
    std::vector<RDNode *> dummy_nodes;
    // RD Blocks mapping
    std::unordered_map<const llvm::Value *, std::unique_ptr<RDBlock>> blocks;
    // all constructed functions and their corresponding blocks
    std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, RDBlock *>> functions_blocks;

public:
    LLVMRDBuilder(const llvm::Module *m,
                  dg::LLVMPointerAnalysis *p,
                  bool pure_funs = false)
        : M(m), DL(new llvm::DataLayout(m)),
          assume_pure_functions(pure_funs), PTA(p) {}
    ~LLVMRDBuilder();

    RDNode *build();

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RDNode *>&
                                getNodesMap() const { return nodes_map; }
    const std::unordered_map<const llvm::Value *, RDNode *>&
                                getMapping() const { return mapping; }

    RDNode *getMapping(const llvm::Value *val)
    {
        auto it = mapping.find(val);
        if (it == mapping.end())
            return nullptr;

        return it->second;
    }

    RDNode *getNode(const llvm::Value *val)
    {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }

    RDNode *getOperand(const llvm::Value *val, RDBlock *rb);
    RDNode *createNode(const llvm::Instruction& Inst, RDBlock *rb);

    const std::unordered_map<const llvm::Value *, std::unique_ptr<RDBlock>>& getBlocks() const {
        return blocks;
    }

    std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, RDBlock *>>& getConstructedFunctions() {
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
        blocks.emplace(std::make_pair(val, std::unique_ptr<RDBlock>(block)));
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

    RDNode *createLoad(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createStore(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createAlloc(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createDynAlloc(const llvm::Instruction *Inst, MemAllocationFuncs type, RDBlock *rb);
    RDNode *createRealloc(const llvm::Instruction *Inst, RDBlock *rb);
    RDNode *createReturn(const llvm::Instruction *Inst, RDBlock *rb);

    RDBlock *buildBlock(const llvm::BasicBlock& block);
    std::pair<RDBlock *,RDBlock *> buildFunction(const llvm::Function& F);

    RDBlock *buildGlobals();

    std::pair<RDNode *, RDNode *>
    createCallToFunction(const llvm::Function *F, RDBlock *rb);

    std::pair<RDNode *, RDNode *>
    createCall(const llvm::Instruction *Inst, RDBlock *rb);

    RDNode *createIntrinsicCall(const llvm::CallInst *CInst, RDBlock *rb);
    RDNode *createUndefinedCall(const llvm::CallInst *CInst, RDBlock *rb);
};

class LLVMReachingDefinitions
{
    std::unique_ptr<LLVMRDBuilder> builder;
    std::unique_ptr<ReachingDefinitionsAnalysis> RDA;
    dg::analysis::rd::ssa::SparseRDGraphBuilder srg_builder;
    dg::analysis::rd::ssa::SparseRDGraph srg;
    RDNode *root;
    bool strong_update_unknown;
    uint32_t max_set_size;
    std::vector<std::unique_ptr<RDNode>> phi_nodes;

public:
    LLVMReachingDefinitions(const llvm::Module *m,
                            dg::LLVMPointerAnalysis *pta,
                            bool strong_updt_unknown = false,
                            bool pure_funs = false,
                            uint32_t max_set_sz = ~((uint32_t) 0))
        : builder(std::unique_ptr<LLVMRDBuilder>(new LLVMRDBuilder(m, pta, pure_funs))),
          strong_update_unknown(strong_updt_unknown), max_set_size(max_set_sz) {}

    void run()
    {
        root = builder->build();
        // calculate dominators, true=calculate also DomFrontiers
        Dominators<RDNode,true> d;
        d.calculate(builder->getConstructedFunctions(), builder->getBlocks());

        auto result = srg_builder.build(root);
        phi_nodes = std::move(result.second);
        srg = std::move(result.first);

        RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(
            /* new ReachingDefinitionsAnalysis(root) */
            new SemisparseRda(srg, root)
            );
        RDA->run();
    }

    RDNode *getRoot() {
        return root;
    }

    RDNode *getNode(const llvm::Value *val)
    {
        return builder->getNode(val);
    }

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RDNode *>&
                                getNodesMap() const
    { return builder->getNodesMap(); }

    const std::unordered_map<const llvm::Value *, RDNode *>&
                                getMapping() const
    { return builder->getMapping(); }

    const std::unordered_map<const llvm::Value *, std::unique_ptr<RDBlock>>& 
        getBlocks() const
        { return builder->getBlocks(); }

    RDNode *getMapping(const llvm::Value *val)
    {
        return builder->getMapping(val);
    }

    const dg::analysis::rd::ssa::SparseRDGraph& getSrg() const { return srg; }
    void getNodes(std::set<RDNode *>& cont)
    {
        assert(RDA);
        // FIXME: this is insane, we should have this method defined here
        // not in RDA
        RDA->getNodes(cont);
    }

    const RDMap& getReachingDefinitions(RDNode *n) const { return n->getReachingDefinitions(); }
    RDMap& getReachingDefinitions(RDNode *n) { return n->getReachingDefinitions(); }
    size_t getReachingDefinitions(RDNode *n, const Offset& off,
                                  const Offset& len, std::set<RDNode *>& ret)
    {
        return n->getReachingDefinitions(n, off, len, ret);
    }
};


} // namespace rd
} // namespace dg
} // namespace analysis

#endif
