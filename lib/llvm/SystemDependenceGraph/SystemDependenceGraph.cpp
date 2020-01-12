#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

struct SDGBuilder {
    SystemDependenceGraph *_llvmsdg;
    llvm::Module *_module;

    SDGBuilder(SystemDependenceGraph *llvmsdg, llvm::Module *m)
    : _llvmsdg(llvmsdg), _module(m) {}

    sdg::DGNodeCall& buildCallNode(sdg::DependenceGraph& dg, llvm::CallInst *CI) {
        auto& node = dg.createCall();
        auto& params = node.getParameters();

        // create actual parameters
        for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
            auto *A = CI->getArgOperand(i);
            llvm::errs() << "Act: " << *A << "\n";
            auto& param = params.createParameter();
            _llvmsdg->addMapping(A, &param);
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
            auto& param = params.createParameter();
            _llvmsdg->addMapping(&arg, &param);
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
            _llvmsdg->addFunMapping(&F, &g);

            buildDG(g, F);
        }
    }
};

void SystemDependenceGraph::buildSDG() {
    DBG(sdg, "Building SDG");
    assert(_module);
    assert(_pta);

    SDGBuilder builder(this, _module);
    // FIXME: build globals
    // builder.buildGlobals();
    DBG(sdg, "FIXME: must build globals");

    builder.buildFuns();

    // set the entry function
    auto *llvmentry = _module->getFunction(_options.entryFunction);
    assert(llvmentry && "Module does not contain the entry function");
    auto* entry = getDG(llvmentry);
    assert(entry && "Did not build the entry function");
    _sdg.setEntry(entry);

    DBG(sdg, "Building SDG finished");
}

} // namespace llvmdg
} // namespace dg
