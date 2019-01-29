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

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

namespace dg {
namespace analysis {
namespace rd {

class LLVMRDBuilder
{
protected:
    const llvm::Module *M;
    const llvm::DataLayout *DL;
    const LLVMReachingDefinitionsAnalysisOptions& _options;

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

    std::map<const llvm::CallInst *, RDNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RDNode *> threadJoinCalls;

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
                  const LLVMReachingDefinitionsAnalysisOptions& opts)
        : M(m), DL(new llvm::DataLayout(m)), _options(opts), PTA(p) {}

    virtual ~LLVMRDBuilder() {
        // delete data layout
        delete DL;

        // delete artificial nodes from subgraphs
        for (auto& it : subgraphs_map) {
            assert((it.second.root && it.second.ret) ||
                   (!it.second.root && !it.second.ret));
            delete it.second.root;
            delete it.second.ret;
        }

        // delete nodes
        for (auto& it : nodes_map) {
            assert(it.first && "Have a nullptr node mapping");
            delete it.second;
        }

        // delete dummy nodes
        for (RDNode *nd : dummy_nodes)
            delete nd;
    }

    virtual ReachingDefinitionsGraph build() = 0;

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
};

}
}
}
#endif // _LLVM_DG_RD_BUILDER_H
