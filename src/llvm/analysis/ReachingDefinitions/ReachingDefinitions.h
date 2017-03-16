#ifndef _LLVM_DG_RD_H_
#define _LLVM_DG_RD_H_

#include <unordered_map>
#include <memory>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "llvm/analysis/PointsTo/PointsTo.h"

namespace dg {
namespace analysis {
namespace rd {

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
    RDNode *createDynAlloc(const llvm::Instruction *Inst, int type);
    RDNode *createRealloc(const llvm::Instruction *Inst);
    RDNode *createReturn(const llvm::Instruction *Inst);

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

class LLVMReachingDefinitions
{
    std::unique_ptr<LLVMRDBuilder> builder;
    std::unique_ptr<ReachingDefinitionsAnalysis> RDA;
    RDNode *root;
    bool strong_update_unknown;
    uint32_t max_set_size;

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
        RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(
            new ReachingDefinitionsAnalysis(root, strong_update_unknown, max_set_size)
            );
        RDA->run();
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

    RDNode *getMapping(const llvm::Value *val)
    {
        return builder->getMapping(val);
    }

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
