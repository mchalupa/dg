#ifndef DG_SVF_POINTER_ANALYSIS_H_
#define DG_SVF_POINTER_ANALYSIS_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreorder"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreorder"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif

#include "WPA/Andersen.h" // Andersen analysis from SVF

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wreorder
#pragma clang diagnostic pop // ignore -Wignored-qualifiers
#pragma clang diagnostic pop // ignore -Woverloaded-virtual
#else
#pragma clang diagnostic pop // ignore -Wreorder
#pragma clang diagnostic pop // ignore -Wignored-qualifiers
#pragma clang diagnostic pop // ignore -Woverloaded-virtual
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/PointsTo/Pointer.h"

#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/PointsTo/LLVMPointsToSet.h"

#ifndef HAVE_SVF
#error "Do not have SVF"
#endif

namespace dg {

using pta::Pointer;

/// Implementation of LLVMPointsToSet that iterates
//  over the DG's points-to set
class SvfLLVMPointsToSet : public LLVMPointsToSetImplTemplate<const PointsTo> {
    PAG *_pag;
    size_t _position{0};

    llvm::Value *_getValue(unsigned id) const {
	    auto pagnode = _pag->getPAGNode(id);
	    if (pagnode->hasValue()) {
	    	return const_cast<llvm::Value*>(pagnode->getValue());
	    } else {
	    	// for debuggin right now
	    	llvm::errs() << "[SVF] No value in PAG NODE\n";
	    	llvm::errs() << *pagnode << "\n";
            return nullptr;
	    }
    }

    void _findNextReal() override {
        while ((it != PTSet.end())) {
            if (_pag->getPAGNode(*it)->hasValue()) {
                break;
            } else {
                llvm::errs() << "no val" << *_pag->getPAGNode(*it) << "\n";
                continue;
            }
            ++it;
            ++_position;
        }
    }

public:
    SvfLLVMPointsToSet(PointsTo& S, PAG *pag)
    : LLVMPointsToSetImplTemplate(std::move(S)), _pag(pag) {
        initialize_iterator();
    }

    bool hasUnknown() const override {
        return PTSet.test(_pag->getBlackHoleNode());
    }

    bool hasNull() const override {
        return PTSet.test(_pag->getNullPtr());
    }

    bool hasInvalidated() const override { return false; }
    size_t size() const override { return PTSet.count(); }

    LLVMPointer getKnownSingleton() const override {
        assert(isKnownSingleton());
        return LLVMPointer(_getValue(*PTSet.begin()), Offset::UNKNOWN);
    }

    LLVMPointer get() const override {
        assert(it != PTSet.end() && "Dereferenced end() iterator");
        return LLVMPointer{_getValue(*it), Offset::UNKNOWN};
    }
};


///
// Integration of pointer analysis from SVF
class SVFPointerAnalysis : public LLVMPointerAnalysis {
    const llvm::Module *_module{nullptr};
    SVFModule _svfModule;
    std::unique_ptr<PointerAnalysis> _pta{};

    PointsTo& getUnknownPTSet() const {
        static PointsTo _unknownPTSet;
        if (_unknownPTSet.empty())
            _unknownPTSet.set(_pta->getPAG()->getBlkPtr());
        return _unknownPTSet;
    }

    LLVMPointsToSet mapSVFPointsTo(PointsTo& S, PAG *pag) {
        SvfLLVMPointsToSet *pts;
        if (S.empty()) {
            pts = new SvfLLVMPointsToSet(getUnknownPTSet(), pag);
        } else {
            pts = new SvfLLVMPointsToSet(S, pag);
        }
        return pts->toLLVMPointsToSet();
    }


public:
    SVFPointerAnalysis(const llvm::Module *M,
                       const LLVMPointerAnalysisOptions& opts)
        : LLVMPointerAnalysis(opts), _module(M),
          _svfModule(const_cast<llvm::Module*>(_module)) {}

    bool hasPointsTo(const llvm::Value *val) override {
		PAG* pag = _pta->getPAG();
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
		PAG* pag = _pta->getPAG();
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
		PAG* pag = _pta->getPAG();
		auto pts = _pta->getPts(pag->getValueNode(val));
        return {!pts.empty(), mapSVFPointsTo(pts, pag)};
    }

    void run() override {
		_pta.reset(new Andersen());
        _pta->analyze(_svfModule);
    }
};

} // namespace dg

#endif // DG_SVF_POINTERS_TO_ANALYSIS_H_
