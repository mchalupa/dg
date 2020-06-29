#ifndef DG_LLVM_NTSCD_H_
#define DG_LLVM_NTSCD_H_

#include <llvm/IR/Module.h>

#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "GraphBuilder.h"

#include <set>
#include <map>
#include <unordered_map>


namespace llvm {
class Function;
}

namespace dg {
namespace llvmdg {

class NTSCD : public LLVMControlDependenceAnalysisImpl {
    CDGraphBuilder graphBuilder{};
    std::unordered_map<const llvm::Function *, CDGraph> _graphs;

public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    NTSCD(const llvm::Module *module,
          const LLVMControlDependenceAnalysisOptions& opts = {})
        : LLVMControlDependenceAnalysisImpl(module, opts) {
        _graphs.reserve(module->size());
    }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *) override { return {}; }
    ValVec getDependent(const llvm::Instruction *) override { return {}; }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
        // XXX: this could be computed on-demand per one node (block)
        // (in contrary to getDependent())
        if (_computed.insert(b->getParent()).second) {
            /// FIXME: get rid of the const cast
            computeOnDemand(const_cast<llvm::Function*>(b->getParent()));
        }

        //auto *block = graphBuilder.mapBlock(b);
        //if (!block) {
        //    return {};
        //}
        //auto it = revControlDependency.find(block->front());
        //if (it == revControlDependency.end())
        //    return {};

        //std::set<llvm::Value *> ret;
        //for (auto *dep : it->second) {
        //    assert(dep->llvmBlock() && "Do not have LLVM block");
        //    ret.insert(const_cast<llvm::BasicBlock*>(dep->llvmBlock()));
        //}

        //return ValVec{ret.begin(), ret.end()};
        return {};
    }

    ValVec getDependent(const llvm::BasicBlock *) override {
        assert(false && "Not supported");
        abort();
    }

    // We run on demand. However, you may use manually computeDependencies()
    // to compute all dependencies in the interprocedural CFG.
    void run() override { /* we run on demand */ }

    CDGraph *getGraph(const llvm::Function *f) override { return _getGraph(f); }
    const CDGraph *getGraph(const llvm::Function *f) const override { return _getGraph(f); }

private:
    const CDGraph *_getGraph(const llvm::Function *f) const {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second;
    }

    CDGraph *_getGraph(const llvm::Function *f) {
        auto it = _graphs.find(f);
        return it == _graphs.end() ? nullptr : &it->second;
    }

   // forward edges (from branchings to dependent blocks)
   //std::map<CDNode *, std::set<CDNode *>> controlDependence;
   // reverse edges (from dependent blocks to branchings)
   //std::map<CDNode *, std::set<CDNode *>> revControlDependence;
   std::set<const llvm::Function *> _computed; // for on-demand

   void computeOnDemand(llvm::Function *F) {
       DBG(cda, "Triggering on-demand computation for " << F->getName().str());
       assert(_getGraph(F) == nullptr
              && "Already have the graph");

       auto graph = graphBuilder.build(F, getOptions().nodePerInstruction());
       _graphs.emplace(F, std::move(graph));
   }
};

} // namespace llvmdg
} // namespace dg

#endif
