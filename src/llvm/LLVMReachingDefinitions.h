#ifndef _LLVM_DG_RD_H_
#define _LLVM_DG_RD_H_

#include <unordered_map>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "analysis/ReachingDefinitions.h"

#include "LLVMPointsToAnalysis.h"

namespace dg {
namespace analysis {
namespace rd {

class LLVMRDBuilder
{
    const llvm::Module *M;
    const llvm::DataLayout *DL;

    // points-to information
    LLVMPointsToAnalysis *PTA;

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, RDNode *> nodes_map;

    // mapping of llvm nodes to relevant reaching definitions nodes
    // (this is a super-set of nodes_map)
    // we could keep just one map of these two and don't duplicate
    // the information, but this way it is more bug-proof
    std::unordered_map<const llvm::Value *, RDNode *> mapping;

public:
    LLVMRDBuilder(const llvm::Module *m, LLVMPointsToAnalysis *p)
        : M(m), DL(new llvm::DataLayout(M->getDataLayout())), PTA(p) {}

    ~LLVMRDBuilder() { delete DL; }

    std::pair<RDNode *, RDNode *>
    buildPSSBlock(const llvm::BasicBlock& block);

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

private:
    void addNode(const llvm::Value *val, RDNode *node)
    {
        nodes_map[val] = node;
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    RDNode *createStore(const llvm::Instruction *Inst);

    std::pair<RDNode *, RDNode *> buildBlock(const llvm::BasicBlock& block);
    RDNode *buildFunction(const llvm::Function& F);
    std::pair<RDNode *, RDNode *> buildGlobals();
};

class LLVMReachingDefinitions
{
    LLVMRDBuilder *builder;
    ReachingDefinitionsAnalysis *RDA;
    RDNode *root;

public:
    LLVMReachingDefinitions(const llvm::Module *m, LLVMPointsToAnalysis *pta)
        : builder(new LLVMRDBuilder(m, pta)) {}

    void run()
    {
        root = builder->build();
        RDA = new ReachingDefinitionsAnalysis(root);
        RDA->run();
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

};


} // namespace rd
} // namespace dg
} // namespace analysis

#endif
