#ifndef _LLVM_DG_RD_H_
#define _LLVM_DG_RD_H_

#include <unordered_map>
#include <memory>
#include <type_traits>

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

#include "llvm/MemAllocationFuncs.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/SemisparseRda.h"
#include "llvm/analysis/PointsTo/PointsTo.h"
#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h"
#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.h"
#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.h"
#include "llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

namespace dg {
namespace analysis {
namespace rd {

namespace detail {
    template <typename Rda>
        struct BuilderSelector {
            using BuilderT = LLVMRDBuilderDense;
        };

    template <>
        struct BuilderSelector<SemisparseRda> {
            using BuilderT = LLVMRDBuilderSemisparse;
        };
}

class LLVMReachingDefinitions
{
    std::unique_ptr<LLVMRDBuilder> builder;
    std::unique_ptr<ReachingDefinitionsAnalysis> RDA;
    RDNode *root;
    const llvm::Module *m;
    dg::LLVMPointerAnalysis *pta;
    const LLVMReachingDefinitionsAnalysisOptions _options;

public:

    LLVMReachingDefinitions(const llvm::Module *m,
                            dg::LLVMPointerAnalysis *pta,
                            const LLVMReachingDefinitionsAnalysisOptions& opts)
        : m(m), pta(pta), _options(opts) {}

    /**
     * Template parameters:
     * RdaType - class extending dg::analysis::rd::ReachingDefinitions to be used as analysis
     */
    template <typename RdaType>
    void run()
    {
        // this helps while guessing causes of template substitution errors
        static_assert(std::is_base_of<ReachingDefinitionsAnalysis, RdaType>::value, "RdaType has to be subclass of ReachingDefinitionsAnalysis");
        using BuilderT = typename detail::BuilderSelector<RdaType>::BuilderT;
        builder = std::unique_ptr<LLVMRDBuilder>(new BuilderT(m, pta, _options));
        root = builder->build();

        RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(new RdaType(root));
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
