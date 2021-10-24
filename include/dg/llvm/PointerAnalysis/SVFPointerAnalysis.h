#ifndef DG_SVF_POINTER_ANALYSIS_H_
#define DG_SVF_POINTER_ANALYSIS_H_

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>

#include <SVF-FE/LLVMModule.h> // LLVMModuleSet
#include <SVF-FE/PAGBuilder.h> // PAGBuilder
#include <WPA/Andersen.h>      // Andersen analysis from SVF

#include "dg/PointerAnalysis/Pointer.h"

#include "dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/PointerAnalysis/LLVMPointsToSet.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/util/debug.h"

namespace dg {

using pta::Pointer;

using SVF::LLVMModuleSet;
using SVF::PAG;
using SVF::PointsTo;

/// Implementation of LLVMPointsToSet that iterates
//  over the DG's points-to set
class SvfLLVMPointsToSet : public LLVMPointsToSetImplTemplate<const PointsTo> {
    PAG *_pag;
    size_t _position{0};

    llvm::Value *_getValue(unsigned id) const {
        auto *pagnode = _pag->getPAGNode(id);
        if (pagnode->hasValue())
            return const_cast<llvm::Value *>(pagnode->getValue());

        // for debugging right now
        llvm::errs() << "[SVF] No value in PAG NODE\n";
        llvm::errs() << *pagnode << "\n";
        return nullptr;
    }

    void _findNextReal() override {
        while (it != PTSet.end()) {
            if (_pag->getPAGNode(*it)->hasValue())
                break;

            // else
            //     llvm::errs() << "no val" << *_pag->getPAGNode(*it) << "\n";

            ++it;
            ++_position;
        }
    }

  public:
    SvfLLVMPointsToSet(PointsTo &S, PAG *pag)
            : LLVMPointsToSetImplTemplate(std::move(S)), _pag(pag) {
        initialize_iterator();
    }

    bool hasUnknown() const override {
        return PTSet.test(_pag->getBlackHoleNode());
    }

    bool hasNull() const override { return PTSet.test(_pag->getNullPtr()); }

    bool hasNullWithOffset() const override {
        // we are field-insensitive now...
        return hasNull();
    }

    bool hasInvalidated() const override { return false; }
    size_t size() const override { return PTSet.count(); }

    LLVMPointer getKnownSingleton() const override {
        assert(isKnownSingleton());
        return {_getValue(*PTSet.begin()), Offset::UNKNOWN};
    }

    LLVMPointer get() const override {
        assert(it != PTSet.end() && "Dereferenced end() iterator");
        return {_getValue(*it), Offset::UNKNOWN};
    }
};

///
// Integration of pointer analysis from SVF
class SVFPointerAnalysis : public LLVMPointerAnalysis {
    const llvm::Module *_module{nullptr};
    SVF::SVFModule *_svfModule{nullptr};
    std::unique_ptr<SVF::PointerAnalysis> _pta{};

    PointsTo &getUnknownPTSet() const {
        static PointsTo _unknownPTSet;
        if (_unknownPTSet.empty())
            _unknownPTSet.set(_pta->getPAG()->getBlackHoleNode());
        return _unknownPTSet;
    }

    LLVMPointsToSet mapSVFPointsTo(PointsTo &S, PAG *pag) {
        auto *pts =
                new SvfLLVMPointsToSet(S.empty() ? getUnknownPTSet() : S, pag);
        return pts->toLLVMPointsToSet();
    }

  public:
    SVFPointerAnalysis(const llvm::Module *M,
                       const LLVMPointerAnalysisOptions &opts)
            : LLVMPointerAnalysis(opts), _module(M) {}

    ~SVFPointerAnalysis() override {
        // _svfModule overtook the ownership of llvm::Module,
        // we must re-take it to avoid double free
        LLVMModuleSet::releaseLLVMModuleSet();
    }

    bool hasPointsTo(const llvm::Value *val) override {
        PAG *pag = _pta->getPAG();
        auto pts = _pta->getPts(pag->getValueNode(val));
        return !pts.empty();
    }

    ///
    // Get the points-to information for the given LLVM value.
    // The return object has methods begin(), end() that can be used
    // for iteration over (llvm::Value *, Offset) pairs of the
    // points-to set. Moreover, the object has methods hasUnknown()
    // and hasNull() that reflect whether the points-to set of the
    // LLVM value contains unknown element of null.
    LLVMPointsToSet getLLVMPointsTo(const llvm::Value *val) override {
        PAG *pag = _pta->getPAG();
        auto pts = _pta->getPts(pag->getValueNode(val));
        return mapSVFPointsTo(pts, pag);
    }

    ///
    // This method is the same as getLLVMPointsTo, but it returns
    // also the information whether the node of pointer analysis exists
    // (opposed to the getLLVMPointsTo, which returns a set with
    // unknown element when the node does not exists)
    std::pair<bool, LLVMPointsToSet>
    getLLVMPointsToChecked(const llvm::Value *val) override {
        PAG *pag = _pta->getPAG();
        auto pts = _pta->getPts(pag->getValueNode(val));
        return {!pts.empty(), mapSVFPointsTo(pts, pag)};
    }

    bool run() override {
        using namespace SVF;

        DBG_SECTION_BEGIN(pta, "Running SVF pointer analysis (Andersen)");

        auto *moduleset = LLVMModuleSet::getLLVMModuleSet();
        _svfModule =
                moduleset->buildSVFModule(*const_cast<llvm::Module *>(_module));
        assert(_svfModule && "Failed building SVF module");

        _svfModule->buildSymbolTableInfo();

        PAGBuilder builder;
        PAG *pag = builder.build(_svfModule);

        _pta.reset(new Andersen(pag));
        _pta->disablePrintStat();
        _pta->analyze();

        DBG_SECTION_END(pta, "Done running SVF pointer analysis (Andersen)");
        return true;
    }
};

} // namespace dg

#endif // DG_SVF_POINTER_ANALYSIS_H_
