#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

struct Builder {
    SystemDependenceGraph *_llvmsdg;
    llvm::Module *_module;
    Builder(SystemDependenceGraph *llvmsdg,
            llvm::Module *m)
    : _llvmsdg(llvmsdg), _module(m) {}

    void buildBBlock(sdg::DependenceGraph *dg, llvm::BasicBlock& B) {
        auto *block = dg->createBBlock();

        for (auto& I : B) {
            sdg::DGNode *node;
            if (llvm::isa<llvm::CallInst>(&I)) {
                node = dg->createCall();
            } else {
                node = dg->createInstruction();
            }
            block->append(node);
            _llvmsdg->addMapping(&I, node);
        }
    }

    void buildDG(sdg::DependenceGraph *dg, llvm::Function& F) {
        DBG_SECTION_BEGIN(sdg, "Building '" << F.getName().str() << "'");
        for (auto& B: F) {
            buildBBlock(dg, B);
        }

        // FIXME: build parameters

        DBG_SECTION_END(sdg, "Building '" << F.getName().str() << "' finished");
    }

    void buildFuns() {
        auto& sdg = _llvmsdg->getSDG();
        // build dependence graph for each procedure
        for (auto& F : *_module) {
            if (F.isDeclaration()) {
                continue;
            }
            auto *g = sdg.createGraph(F.getName().str());
            buildDG(g, F);
        }
    }
};

void SystemDependenceGraph::buildSDG() {
    DBG(sdg, "Building SDG");
    assert(_module);
    assert(_pta);

    Builder builder(this, _module);
    // FIXME: build globals
    // builder.buildGlobals();

    builder.buildFuns();

    DBG(sdg, "Building SDG finished");
}

} // namespace llvmdg
} // namespace dg
