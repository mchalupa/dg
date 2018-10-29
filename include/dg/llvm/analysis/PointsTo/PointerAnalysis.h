#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/PointsTo/PointerSubgraph.h"
#include "dg/analysis/PointsTo/PointerAnalysis.h"
#include "dg/analysis/PointsTo/PointerSubgraphOptimizations.h"
#include "dg/analysis/PointsTo/PointerAnalysisFSInv.h"

#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerSubgraph.h"


namespace dg {

using analysis::LLVMPointerAnalysisOptions;
using analysis::pta::PointerSubgraph;
using analysis::pta::PSNode;
using analysis::pta::LLVMPointerSubgraphBuilder;
using analysis::pta::PSNodesSeq;
using analysis::Offset;

template <typename PTType>
class LLVMPointerAnalysisImpl : public PTType
{
    LLVMPointerSubgraphBuilder *builder;

public:
    LLVMPointerAnalysisImpl(PointerSubgraph *PS, LLVMPointerSubgraphBuilder *b)
    : PTType(PS), builder(b) {}

    // build new subgraphs on calls via pointer
    bool functionPointerCall(PSNode *callsite, PSNode *called) override
    {
        const llvm::Function *F
            = llvm::dyn_cast<llvm::Function>(called->getUserData<llvm::Value>());
        // with vararg it may happen that we get pointer that
        // is not to function, so just bail out here in that case
        if (!F)
            return false;

        if (F->isDeclaration()) {
            // calling declaration that returns a pointer?
            // That is unknown pointer
            return callsite->getPairedNode()->addPointsTo(analysis::pta::PointerUnknown);
        }

        if (!LLVMPointerSubgraphBuilder::callIsCompatible(callsite, called))
            return false;

        builder->insertFunctionCall(callsite, called);

#ifndef NDEBUG
        // check the graph after rebuilding, but do not check for connectivity,
        // because we can call a function that will disconnect the graph
        if (!builder->validateSubgraph(true)) {
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after building this function called via pointer: "
                         <<  F->getName() << "\n";
            abort();
        }
#endif // NDEBUG

        return true; // we changed the graph
    }
};

class LLVMPointerAnalysis
{
    PointerSubgraph *PS = nullptr;
    std::unique_ptr<LLVMPointerSubgraphBuilder> _builder;

    LLVMPointerAnalysisOptions createOptions(const char *entry_func,
                                             uint64_t field_sensitivity)
    {
        LLVMPointerAnalysisOptions opts;
        opts.setFieldSensitivity(field_sensitivity);
        opts.setEntryFunction(entry_func);
        return opts;
    }

public:

    LLVMPointerAnalysis(const llvm::Module *m,
                        const char *entry_func = "main",
                        uint64_t field_sensitivity = Offset::UNKNOWN)
        : LLVMPointerAnalysis(m, createOptions(entry_func, field_sensitivity)) {}

    LLVMPointerAnalysis(const llvm::Module *m, const LLVMPointerAnalysisOptions opts)
        : _builder(new LLVMPointerSubgraphBuilder(m, opts)) {}

    PSNode *getPointsTo(const llvm::Value *val)
    {
        return _builder->getPointsTo(val);
    }

    const std::unordered_map<const llvm::Value *, PSNodesSeq>&
    getNodesMap() const
    {
        return _builder->getNodesMap();
    }

    const std::vector<std::unique_ptr<PSNode>>& getNodes()
    {
        return PS->getNodes();
    }

    std::vector<PSNode *> getFunctionNodes(const llvm::Function *F) const {
        return _builder->getFunctionNodes(F);
    }

    PointerSubgraph *getPS() { return PS; }
    const PointerSubgraph *getPS() const { return PS; }

    void buildSubgraph()
    {
        // run the analysis itself
        assert(_builder && "Incorrectly constructed PTA, missing builder");

        PS = _builder->buildLLVMPointerSubgraph();
        if (!PS) {
            llvm::errs() << "Pointer Subgraph was not built, aborting\n";
            abort();
        }

/*
        analysis::pta::PointerSubgraphOptimizer optimizer(PS);
        optimizer.run();

        if (optimizer.getNumOfRemovedNodes() > 0)
            _builder->composeMapping(std::move(optimizer.getMapping()));

        llvm::errs() << "PS optimization removed " << optimizer.getNumOfRemovedNodes() << " nodes\n";

#ifndef NDEBUG
        analysis::pta::debug::LLVMPointerSubgraphValidator validator(_builder->getPS());
        if (validator.validate()) {
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after optimizing the graph.";
            assert(!validator.getErrors().empty());
            llvm::errs() << validator.getErrors();
            abort();
        }
#endif // NDEBUG
*/

    }

    template <typename PTType>
    void run()
    {
        buildSubgraph();

        LLVMPointerAnalysisImpl<PTType> PTA(PS, _builder.get());
        PTA.run();
    }

    // this method creates PointerAnalysis object and returns it.
    // It is alternative to run() method, but it does not delete all
    // the analysis data as the run() (like memory objects and so on).
    // run() preserves only PointerSubgraph and the builder
    template <typename PTType>
    analysis::pta::PointerAnalysis *createPTA()
    {
        buildSubgraph();
        return new LLVMPointerAnalysisImpl<PTType>(PS, _builder.get());
    }
};

template <>
inline void LLVMPointerAnalysis::run<analysis::pta::PointerAnalysisFSInv>()
{
    // build the subgraph
    assert(_builder && "Incorrectly constructed PTA, missing builder");
    _builder->setInvalidateNodesFlag(true);
    buildSubgraph();

    LLVMPointerAnalysisImpl<analysis::pta::PointerAnalysisFSInv> PTA(PS, _builder.get());
    PTA.run();
}

template <>
inline analysis::pta::PointerAnalysis *LLVMPointerAnalysis::createPTA<analysis::pta::PointerAnalysisFSInv>()
{
    // build the subgraph
    assert(_builder && "Incorrectly constructed PTA, missing builder");
    _builder->setInvalidateNodesFlag(true);
    buildSubgraph();

    return new LLVMPointerAnalysisImpl<analysis::pta::PointerAnalysisFSInv>(PS, _builder.get());
}

} // namespace dg

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
