#ifndef _DG_TOOL_LLVM_SLICER_H_
#define _DG_TOOL_LLVM_SLICER_H_

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/LLVMDependenceGraphBuilder.h"
#include "llvm/LLVMDGAssemblyAnnotationWriter.h"
#include "llvm/LLVMSlicer.h"

#include "llvm-slicer-opts.h"

#include "TimeMeasure.h"

/// --------------------------------------------------------------------
//   - Slicer class -
//
//  The main class that takes the bitcode, constructs the dependence graph
//  and then slices it w.r.t given slicing criteria.
/// --------------------------------------------------------------------
class Slicer {
    llvm::Module *M{};
    const SlicerOptions& _options;

    dg::llvmdg::LLVMDependenceGraphBuilder _builder;
    std::unique_ptr<dg::LLVMDependenceGraph> _dg{};

    dg::LLVMSlicer slicer;
    uint32_t slice_id = 0;

public:
    Slicer(llvm::Module *mod, const SlicerOptions& opts)
    : M(mod), _options(opts),
      _builder(mod, _options.dgOptions) { assert(mod && "Need module"); }

    const dg::LLVMDependenceGraph& getDG() const { return *_dg.get(); }
    dg::LLVMDependenceGraph& getDG() { return *_dg.get(); }


    // mark the nodes from the slice
    bool mark(std::set<dg::LLVMNode *>& criteria_nodes,
              bool always_compute_deps = false)
    {
        assert(_dg && "mark() called without the dependence graph built");

        dg::debug::TimeMeasure tm;

        // if we found slicing criterion, compute the rest
        // of the graph. Otherwise just slice away the whole graph
        if (!criteria_nodes.empty() || always_compute_deps)
            _dg = _builder.computeDependencies(std::move(_dg));

        // don't go through the graph when we know the result:
        // only empty main will stay there. Just delete the body
        // of main and keep the return value
        if (criteria_nodes.empty())
            return createNewMain();

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
private:

    ///
    // Create new empty main. If 'call_entry' is set to true, then
    // call the entry function from the new main, otherwise the
    // main is going to be empty
    bool createNewMain()
    {
        llvm::Function *main_func = M->getFunction("main");
        if (!main_func) {
            llvm::errs() << "No main function found in module. This seems like bug since\n"
                            "here we should have the graph build from main\n";
            return false;
        }
    
        // delete old function body
        main_func->deleteBody();
    
        // create new function body that just returns
        llvm::LLVMContext& ctx = M->getContext();
        llvm::BasicBlock* blk = llvm::BasicBlock::Create(ctx, "entry", main_func);
    
        if (_options.dgOptions.entryFunction != "main") {
            llvm::Function *entry = M->getFunction(_options.dgOptions.entryFunction);
            assert(entry && "The entry function is not present in the module");
    
            // TODO: we should set the arguments to undef
            llvm::CallInst::Create(entry, "entry", blk);
        }
    
        llvm::Type *Ty = main_func->getReturnType();
        llvm::Value *retval = nullptr;
        if (Ty->isIntegerTy())
            retval = llvm::ConstantInt::get(Ty, 0);
        llvm::ReturnInst::Create(ctx, retval, blk);
    
        return true;
    }
};

#endif // _DG_TOOL_LLVM_SLICER_H_
