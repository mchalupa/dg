#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

#include "llvm/llvm-utils.h"

namespace dg {
namespace llvmdg {

struct SDGBuilder {
    SystemDependenceGraph *_llvmsdg;
    llvm::Module *_module;

    SDGBuilder(SystemDependenceGraph *llvmsdg, llvm::Module *m)
            : _llvmsdg(llvmsdg), _module(m) {}

    sdg::DependenceGraph &getOrCreateDG(llvm::Function *F) {
        auto *dg = _llvmsdg->getDG(F);
        if (!dg) {
            auto &g = _llvmsdg->getSDG().createGraph(F->getName().str());
            _llvmsdg->addFunMapping(F, &g);
            return g;
        }

        return *dg;
    }

    sdg::DGNode &buildCallNode(sdg::DependenceGraph &dg, llvm::CallInst *CI) {
#if LLVM_VERSION_MAJOR >= 8
        auto *CV = CI->getCalledOperand()->stripPointerCasts();
#else
        auto *CV = CI->getCalledValue()->stripPointerCasts();
#endif
        if (!CV) {
            assert(false && "funcptr not implemnted yet");
            abort();
        }

        auto *F = llvm::dyn_cast<llvm::Function>(CV);
        if (!F) {
            assert(false && "funcptr not implemnted yet");
            abort();
        }

        if (F->isDeclaration()) {
            return dg.createInstruction();
        }

        // create the node call and and the call edge
        auto &node = dg.createCall();
        node.addCallee(getOrCreateDG(F));

        // create actual parameters
        auto &params = node.getParameters();
        for (const auto &arg : llvmutils::args(CI)) {
            llvm::errs() << "Act: " << *arg << "\n";
            params.createParameter();
        }
        return node;
    }

    void buildBBlock(sdg::DependenceGraph &dg, llvm::BasicBlock &B) {
        auto &block = dg.createBBlock();
        _llvmsdg->addBlkMapping(&B, &block);

        for (auto &I : B) {
            sdg::DGNode *node = nullptr;
            if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&I)) {
                node = &buildCallNode(dg, CI);
            } else {
                node = &dg.createInstruction();
                if (llvm::isa<llvm::ReturnInst>(I)) {
                    // add formal 'ret' parameter
                    auto &params = dg.getParameters();
                    auto *ret = params.getReturn();
                    if (!ret)
                        ret = &params.createReturn();
                    // XXX: do we want the 'use' edge here?
                    node->addUser(*ret);
                }
            }
            assert(node && "Failed creating a node");

            block.append(node);
            _llvmsdg->addMapping(&I, node);
        }
    }

    void buildFormalParameters(sdg::DependenceGraph &dg, llvm::Function &F) {
        DBG(sdg, "Building parameters for '" << F.getName().str() << "'");
        auto &params = dg.getParameters();

        if (F.isVarArg()) {
            params.createVarArg();
        }

        for (auto &arg : F.args()) {
            llvm::errs() << "Form: " << arg << "\n";
            auto &param = params.createParameter();
            _llvmsdg->addMapping(&arg, &param);
        }
    }

    void buildDG(sdg::DependenceGraph &dg, llvm::Function &F) {
        DBG_SECTION_BEGIN(sdg, "Building '" << F.getName().str() << "'");

        buildFormalParameters(dg, F);

        for (auto &B : F) {
            buildBBlock(dg, B);
        }

        DBG_SECTION_END(sdg, "Building '" << F.getName().str() << "' finished");
    }

    void buildGlobals(sdg::DependenceGraph &entry) {
        DBG_SECTION_BEGIN(sdg, "Building globals");

        // globals are formal parameters of the entry function
        auto &params = entry.getParameters();
        for (auto &GV : _module->globals()) {
            auto &g = params.createParameter();
            _llvmsdg->addMapping(&GV, &g);
            llvm::errs() << "GV: " << GV << "\n";
        }
        DBG_SECTION_END(sdg, "Finished building globals");
    }

    void buildFuns() {
        DBG_SECTION_BEGIN(sdg, "Building functions");
        // build dependence graph for each procedure
        for (auto &F : *_module) {
            if (F.isDeclaration()) {
                continue;
            }

            auto &g = getOrCreateDG(&F);
            buildDG(g, F);
        }
        DBG_SECTION_END(sdg, "Done building functions");
    }
};

void SystemDependenceGraph::buildNodes() {
    DBG_SECTION_BEGIN(sdg, "Building SDG nodes");
    assert(_module);
    assert(_pta);

    SDGBuilder builder(this, _module);

    builder.buildFuns();

    // set the entry function
    auto *llvmentry = _module->getFunction(_options.entryFunction);
    assert(llvmentry && "Module does not contain the entry function");
    auto *entry = getDG(llvmentry);
    assert(entry && "Did not build the entry function");
    _sdg.setEntry(entry);

    // only after funs, we need the entry fun created
    builder.buildGlobals(*entry);

    DBG_SECTION_END(sdg, "Building SDG nodes finished");
}

void SystemDependenceGraph::buildSDG() {
    buildNodes();
    // defined in Dependencies.cpp
    buildEdges();
}

} // namespace llvmdg
} // namespace dg
