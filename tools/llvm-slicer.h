#ifndef _DG_TOOL_LLVM_SLICER_H_
#define _DG_TOOL_LLVM_SLICER_H_

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/LLVMDependenceGraphBuilder.h"
#include "llvm/LLVMDGAssemblyAnnotationWriter.h"
#include "llvm/Slicer.h"

#include "TimeMeasure.h"


static std::set<dg::LLVMNode *> getSlicingCriteriaNodes(dg::LLVMDependenceGraph& dg);
static bool createNewMain(llvm::Module *M, bool call_entry = false);
static void annotate(dg::LLVMDependenceGraph *dg,
                     dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT opts,
                     const std::set<dg::LLVMNode *> *criteria);

struct SlicerOptions {
    dg::llvmdg::LLVMDependenceGraphOptions dgOptions{};

    std::vector<std::string> untouchedFunctions{};
    // FIXME: get rid of this once we got the secondary SC
    std::vector<std::string> additionalSlicingCriteria{};

    // slice away also the slicing criteria nodes
    // (if they are not dependent on themselves)
    bool removeSlicingCriteria{false};

    // do we perform forward slicing?
    bool forwardSlicing{false};
};


/// --------------------------------------------------------------------
//   - Slicer class -
//
//  The main class that takes the bitcode, constructs the dependence graph
//  and then slices it w.r.t given slicing criteria.
/// --------------------------------------------------------------------
class Slicer {
    using AnnotationOptsT
            = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;

    llvm::Module *M{};
    const SlicerOptions& _options;
    AnnotationOptsT _annotationOptions{};

    dg::llvmdg::LLVMDependenceGraphBuilder _builder;
    std::unique_ptr<dg::LLVMDependenceGraph> _dg{};

    dg::LLVMSlicer slicer;

    uint32_t slice_id = 0;

public:
    Slicer(llvm::Module *mod,
           const SlicerOptions& opts,
           AnnotationOptsT ao)
    : M(mod), _options(opts),
      _annotationOptions(ao),
      _builder(mod, _options.dgOptions) { assert(mod && "Need module"); }

    const dg::LLVMDependenceGraph& getDG() const { return *_dg.get(); }
    dg::LLVMDependenceGraph& getDG() { return *_dg.get(); }


    // mark the nodes from the slice
    bool mark(std::set<dg::LLVMNode *>& criteria_nodes)
    {
        assert(_dg && "mark() called without the dependence graph built");

        dg::debug::TimeMeasure tm;

        // if we found slicing criterion, compute the rest
        // of the graph. Otherwise just slice away the whole graph
        // Also compute the edges when the user wants to annotate
        // the file - due to debugging.
        if (!criteria_nodes.empty() || (_annotationOptions != 0))
            _dg = _builder.computeDependencies(std::move(_dg));

        // don't go through the graph when we know the result:
        // only empty main will stay there. Just delete the body
        // of main and keep the return value
        if (criteria_nodes.empty())
            return createNewMain(M, _options.dgOptions.entryFunction != "main");

        // unmark this set of nodes after marking the relevant ones.
        // Used to mimic the Weissers algorithm
        std::set<dg::LLVMNode *> unmark;

        if (_options.removeSlicingCriteria)
            unmark = criteria_nodes;

        _dg->getCallSites(_options.additionalSlicingCriteria, &criteria_nodes);

        // do not slice __VERIFIER_assume at all
        // FIXME: do this optional
        for (auto& funcName : _options.untouchedFunctions)
            slicer.keepFunctionUntouched(funcName.c_str());

        slice_id = 0xdead;

        tm.start();
        for (dg::LLVMNode *start : criteria_nodes)
            slice_id = slicer.mark(start, slice_id, _options.forwardSlicing);

        assert(slice_id != 0 && "Somethig went wrong when marking nodes");

        // if we have some nodes in the unmark set, unmark them
        for (dg::LLVMNode *nd : unmark)
            nd->setSlice(0);

        tm.stop();
        tm.report("INFO: Finding dependent nodes took");

        // print debugging llvm IR if user asked for it
        if (_annotationOptions != 0)
            annotate(_dg.get(), _annotationOptions,
                     &criteria_nodes);

        return true;
    }

    bool slice()
    {
        assert(slice_id != 0 && "Must run mark() method before slice()");

        dg::debug::TimeMeasure tm;

        tm.start();
        slicer.slice(_dg.get(), nullptr, slice_id);

        tm.stop();
        tm.report("INFO: Slicing dependence graph took");

        dg::analysis::SlicerStatistics& st = slicer.getStatistics();
        llvm::errs() << "INFO: Sliced away " << st.nodesRemoved
                     << " from " << st.nodesTotal << " nodes in DG\n";

        return true;
    }

    bool buildDG() {
        _dg = std::move(_builder.constructCFGOnly());

        if (!_dg) {
            llvm::errs() << "Building the dependence graph failed!\n";
            return false;
        }

        return true;
    }
};

#endif // _DG_TOOL_LLVM_SLICER_H_
