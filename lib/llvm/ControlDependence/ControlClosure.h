#ifndef DG_LLVM_CONTROL_CLOSURE_H_
#define DG_LLVM_CONTROL_CLOSURE_H_

#include <llvm/IR/Module.h>

#include "GraphBuilder.h"
#include "dg/llvm/ControlDependence/ControlDependence.h"

#include "ControlDependence/ControlClosure.h"

#include <map>
#include <set>
#include <unordered_map>

namespace llvm {
class Function;
}

namespace dg {
namespace llvmdg {

class StrongControlClosure : public LLVMControlDependenceAnalysisImpl {
    CDGraphBuilder graphBuilder{};

    using CDResultT = std::map<CDNode *, std::set<CDNode *>>;

    struct Info {
        CDGraph graph;

        //// forward edges (from branchings to dependent blocks)
        // CDResultT controlDependence{};
        //// reverse edges (from dependent blocks to branchings)
        // CDResultT revControlDependence{};

        Info(CDGraph &&graph) : graph(std::move(graph)) {}
    };

    std::unordered_map<const llvm::Function *, Info> _graphs;

  public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    StrongControlClosure(const llvm::Module *module,
                         const LLVMControlDependenceAnalysisOptions &opts = {})
            : LLVMControlDependenceAnalysisImpl(module, opts) {
        _graphs.reserve(module->size());
    }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction * /*unused*/) override {
        assert(false && "Not supported");
        abort();
    }

    ValVec getDependent(const llvm::Instruction * /*unused*/) override {
        assert(false && "Not supported");
        abort();
    }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock * /*unused*/) override {
        assert(false && "Not supported");
        abort();
    }

    ValVec getDependent(const llvm::BasicBlock * /*unused*/) override {
        assert(false && "Not supported");
        abort();
    }

    ValVec getClosure(const llvm::Function *F,
                      const std::set<llvm::Value *> &vals) override {
        DBG(cda,
            "Computing closure of nodes in function " << F->getName().str());
        auto *graph = getGraph(F);
        if (!graph) {
            auto tmpgraph =
                    graphBuilder.build(F, getOptions().nodePerInstruction());
            // FIXME: we can actually just forget the graph if we do not want to
            // dump it to the user
            auto it = _graphs.emplace(F, std::move(tmpgraph));
            graph = &it.first->second.graph;
        }

        assert(graph);

        // TODO map values...
        dg::StrongControlClosure sclosure;
        std::set<CDNode *> X;
        for (auto *v : vals) {
            X.insert(graphBuilder.getNode(v));
        }
        auto cls = sclosure.getClosure(*graph, X);
        std::vector<llvm::Value *> retval;
        for (auto *n : cls) {
            retval.push_back(
                    const_cast<llvm::Value *>(graphBuilder.getValue(n)));
        }
        return retval;
    }

    // We run on demand
    void compute(const llvm::Function *F) override {
        unsigned n = 0;
        for (const auto &B : *F) {
            if (n == F->size() / 2)
                getClosure(F, {const_cast<llvm::BasicBlock *>(&B)});
            ++n;
        }
        /* we run on demand */
    }

    CDGraph *getGraph(const llvm::Function *f) override { return _getGraph(f); }
    const CDGraph *getGraph(const llvm::Function *f) const override {
        return _getGraph(f);
    }

    // make this one public so that we can dump it in llvm-cda-dump
    // (keep the _ prefix so that we can see that it should not be normally
    // used...)
    const Info *_getFunInfo(const llvm::Function *f) const {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second;
    }

    Info *_getFunInfo(const llvm::Function *f) {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second;
    }

  private:
    const CDGraph *_getGraph(const llvm::Function *f) const {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second.graph;
    }

    CDGraph *_getGraph(const llvm::Function *f) {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second.graph;
    }
};

} // namespace llvmdg
} // namespace dg

#endif
