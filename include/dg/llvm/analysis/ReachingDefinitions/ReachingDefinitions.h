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

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/ReachingDefinitions/SemisparseRda.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

namespace dg {
namespace analysis {
namespace rd {

class SemisparseRda;
class LLVMRDBuilder;

class LLVMReachingDefinitions
{
    LLVMRDBuilder *builder{nullptr};
    std::unique_ptr<ReachingDefinitionsAnalysis> RDA;
    RDNode *root{nullptr};
    const llvm::Module *m;
    dg::LLVMPointerAnalysis *pta;
    const LLVMReachingDefinitionsAnalysisOptions _options;

    void initializeSparseRDA();
    void initializeDenseRDA();

public:

    LLVMReachingDefinitions(const llvm::Module *m,
                            dg::LLVMPointerAnalysis *pta,
                            const LLVMReachingDefinitionsAnalysisOptions& opts)
        : m(m), pta(pta), _options(opts) {}

    ~LLVMReachingDefinitions();

    /**
     * Template parameters:
     * RdaType - class extending dg::analysis::rd::ReachingDefinitions to be used as analysis
     */
    template <typename RdaType>
    void run()
    {
        // this helps while guessing causes of template substitution errors
        static_assert(std::is_base_of<ReachingDefinitionsAnalysis, RdaType>::value,
                      "RdaType has to be subclass of ReachingDefinitionsAnalysis");

        if (std::is_same<RdaType, SemisparseRda>::value) {
            initializeSparseRDA();
        } else {
            initializeDenseRDA();
        }

        assert(builder);
        assert(RDA);
        assert(root);

        RDA->run();
    }

    RDNode *getRoot();
    RDNode *getNode(const llvm::Value *val);

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RDNode *>& getNodesMap() const;
    const std::unordered_map<const llvm::Value *, RDNode *>& getMapping() const;

    RDNode *getMapping(const llvm::Value *val);
    const RDNode *getMapping(const llvm::Value *val) const;

    void getNodes(std::set<RDNode *>& cont)
    {
        assert(RDA);
        // FIXME: this is insane, we should have this method defined here
        // not in RDA
        RDA->getNodes(cont);
    }

    const RDMap& getReachingDefinitions(RDNode *n) const {
        return n->getReachingDefinitions();
    }
    RDMap& getReachingDefinitions(RDNode *n) { return n->getReachingDefinitions(); }
    size_t getReachingDefinitions(RDNode *n, const Offset& off,
                                  const Offset& len, std::set<RDNode *>& ret) {
        return n->getReachingDefinitions(n, off, len, ret);
    }

    std::set<llvm::Value *>
    getLLVMReachingDefinitions(llvm::Value *where, llvm::Value *what,
                               const Offset offset, const Offset len);
};


} // namespace rd
} // namespace dg
} // namespace analysis

#endif
