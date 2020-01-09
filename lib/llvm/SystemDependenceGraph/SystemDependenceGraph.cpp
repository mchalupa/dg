#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

struct Builder {
    SystemDependenceGraph *_llvmsdg;
    llvm::Module *_module;

    Builder(SystemDependenceGraph *llvmsdg, llvm::Module *m)
    : _llvmsdg(llvmsdg), _module(m) {}

    sdg::DGNodeCall& buildCallNode(sdg::DependenceGraph& dg, llvm::CallInst *CI) {
        auto& node = dg.createCall();

        // create actual parameters
        for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
            auto *A = CI->getArgOperand(i);
            llvm::errs() << "Act: " << *A << "\n";
        }
        return node;
    }

    void buildBBlock(sdg::DependenceGraph& dg, llvm::BasicBlock& B) {
        auto& block = dg.createBBlock();

        for (auto& I : B) {
            sdg::DGNode *node;
            if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&I)) {
                node = &buildCallNode(dg, CI);
            } else {
                node = &dg.createInstruction();
            }
            block.append(node);
            _llvmsdg->addMapping(&I, node);
        }
    }

    void buildFormalParameters(sdg::DependenceGraph& dg, llvm::Function& F) {
        DBG(sdg, "Building parameters for '" << F.getName().str() << "'");
        auto& params = dg.getParameters();

        if (F.isVarArg()) {
            params.createVarArg();
        }

        for (auto& arg : F.args()) {
            llvm::errs() << "Form: " << arg << "\n";

            /*
            auto& P = params.createParameter();
            P.inputNode();
            P.outputNode();
            */
        }
    }

    void buildDG(sdg::DependenceGraph& dg, llvm::Function& F) {
        DBG_SECTION_BEGIN(sdg, "Building '" << F.getName().str() << "'");

        buildFormalParameters(dg, F);

        for (auto& B: F) {
            buildBBlock(dg, B);
        }

        DBG_SECTION_END(sdg, "Building '" << F.getName().str() << "' finished");
    }

    void buildFuns() {
        auto& sdg = _llvmsdg->getSDG();
        // build dependence graph for each procedure
        for (auto& F : *_module) {
            if (F.isDeclaration()) {
                continue;
            }
            auto& g = sdg.createGraph(F.getName().str());
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
