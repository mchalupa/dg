#ifndef LLVM_DG_SVF_DD_H_
#define LLVM_DG_SVF_DD_H_

#ifndef HAVE_SVF
#error "Do not have SVF"
#endif

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_os_ostream.h>

#include <SVF-FE/LLVMModule.h> // LLVMModuleSet
#include <SVF-FE/PAGBuilder.h> // PAGBuilder
#include <WPA/Andersen.h>      // Andersen analysis from SVF

#include "dg/DataDependence/DataDependence.h"
#include "dg/ADT/SetQueue.h"

#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

using SVF::LLVMModuleSet;
using SVF::PAG;
using SVF::SVFG;

class SVFLLVMDataDependenceAnalysis : public LLVMDataDependenceAnalysis {
    SVF::SVFModule *_svfModule{nullptr};
    std::unique_ptr<SVF::PointerAnalysis> _pta{};
    SVFG *svfg{nullptr};
    PAG *pag{nullptr};

  public:
    SVFLLVMDataDependenceAnalysis(const llvm::Module *m,
                                  const LLVMDataDependenceAnalysisOptions& opts = {})
            : LLVMDataDependenceAnalysis(m, opts) {}

    ~SVFLLVMDataDependenceAnalysis() override {
        // _svfModule overtook the ovenership of llvm::Module,
        // we must re-take it to avoid double free
        delete svfg;
        LLVMModuleSet::releaseLLVMModuleSet();
    }

    void run() override {
        using namespace SVF;

        DBG_SECTION_BEGIN(dda, "Running SVF pointer analysis (Andersen)");

        auto moduleset = LLVMModuleSet::getLLVMModuleSet();
        _svfModule =
                moduleset->buildSVFModule(*const_cast<llvm::Module *>(getModule()));
        assert(_svfModule && "Failed building SVF module");

        PAGBuilder builder;
        pag = builder.build(_svfModule);
        auto *anders = AndersenWaveDiff::createAndersenWaveDiff(pag);
        anders->disablePrintStat();

        DBG_SECTION_END(dda, "Done running SVF pointer analysis (Andersen)");
 
        DBG_SECTION_BEGIN(dda, "Running SVFG construction");
        SVF::SVFGBuilder svfgBuilder;
        svfg = svfgBuilder.buildFullSVFG(anders);
        //mssa = svfgBuilder.buildMSSA(anders, /* ptrOnlySSA */ = false);
        DBG_SECTION_END(dda, "Finished SVFG construction");
    }


    // return instructions that define the given value
    // (the value must read from memory, e.g. LoadInst)
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Value *use) override {
        ADT::SetQueue<ADT::QueueFIFO<const SVF::VFGNode *>> queue;
        std::vector<llvm::Value *> retval;

        assert(isUse(use) && "The given value is not a use");
        for (auto *vfgnode : svfg->fromValue(use)) {
            queue.push(vfgnode);
        }

        while (!queue.empty()) {
            auto *nd = queue.pop();
            // TODO: SVF does not handle strong updates as we do,
            // but we can at least try to prune the def-use edges
            // on our own using must-alias information...
            for(auto *edge : nd->getInEdges()) {
                //llvm::errs() << *edge << "\n";
                auto *src = edge->getSrcNode();
                auto *val = src->getValue();
                if (val) {
                    if (isDef(val))
                        retval.push_back(const_cast<llvm::Value *>(val));
                }

                // not a real node, search further
                queue.push(src);
            }
        }

        return retval;
    }
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Instruction */* where */,
                                                  llvm::Value */* mem */,
                                                  const Offset &/* off */,
                                                  const Offset &/* len */) override {
        assert(false && "Unsupported");
        abort();
    }
};

} // namespace dda
} // namespace dg

#endif
