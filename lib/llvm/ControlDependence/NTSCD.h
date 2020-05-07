#ifndef DG_LLVM_NTSCD_H_
#define DG_LLVM_NTSCD_H_

#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "GraphBuilder.h"

#include <set>
#include <map>
#include <unordered_map>

#include "Block.h"

namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

class NTSCD : public LLVMControlDependenceAnalysisImpl {
public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    struct NodeInfo {
        bool visited = false;
        bool red = false;
        size_t outDegreeCounter = 0;
    };

    NTSCD(const llvm::Module *module,
          const LLVMControlDependenceAnalysisOptions& opts = {},
          LLVMPointerAnalysis *pointsToAnalysis = nullptr);

    void dump(std::ostream & ostream) const;
    void dumpDependencies(std::ostream & ostream) const;

    const std::map<Block *, std::set<Block *>>&
    controlDependencies() const { return controlDependency; }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Value *) override { return {}; }
    ValVec getDependent(const llvm::Value *) override { return {}; }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
        auto *block = graphBuilder.mapBlock(b);
        if (!block) {
            return {};
        }
        auto it = revControlDependency.find(block->front());
        if (it == revControlDependency.end())
            return {};

        std::set<llvm::Value *> ret;
        for (auto *dep : it->second) {
            assert(dep->llvmBlock() && "Do not have LLVM block");
            ret.insert(const_cast<llvm::BasicBlock*>(dep->llvmBlock()));
        }

        return ValVec{ret.begin(), ret.end()};
    }

    ValVec getDependent(const llvm::BasicBlock *) override {
        assert(false && "Not supported");
        abort();
    }

    void run() override { computeDependencies(); }

private:
    GraphBuilder graphBuilder;
    // forward edges (from branchings to dependent blocks)
    std::map<Block *, std::set<Block *>> controlDependency;
    // reverse edges (from dependent blocks to branchings)
    std::map<Block *, std::set<Block *>> revControlDependency;
    std::unordered_map<Block *, NodeInfo> nodeInfo;

    void computeDependencies();
    void computeDependencies(Function *);

    // a is CD on b
    void addControlDependence(Block *a, Block *b);

    void visitInitialNode(Block * node);
    void visit(Block * node);

    bool hasRedAndNonRedSuccessor(Block * node);
};

} // namespace llvmdg
} // namespace dg

#endif
