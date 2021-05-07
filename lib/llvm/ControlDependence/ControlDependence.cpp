#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "llvm/ControlDependence/ControlClosure.h"
#include "llvm/ControlDependence/DOD.h"
#include "llvm/ControlDependence/InterproceduralCD.h"
#include "llvm/ControlDependence/NTSCD.h"
#include "llvm/ControlDependence/SCD.h"
#include "llvm/ControlDependence/legacy/NTSCD.h"

namespace dg {

void LLVMControlDependenceAnalysis::initializeImpl(LLVMPointerAnalysis *pta,
                                                   llvmdg::CallGraph *cg) {
    bool icfg = getOptions().ICFG();

    if (getOptions().standardCD()) {
        if (icfg) {
            assert(false && "SCD does not support ICFG");
            abort();
        }
        _impl.reset(new llvmdg::SCD(_module, _options));
    } else if (getOptions().ntscdCD() || getOptions().ntscd2CD() ||
               getOptions().ntscdRanganathCD() ||
               getOptions().ntscdRanganathOrigCD()) {
        if (icfg) {
            _impl.reset(new llvmdg::InterproceduralNTSCD(_module, _options, pta,
                                                         cg));
        } else {
            _impl.reset(new llvmdg::NTSCD(_module, _options));
        }
    } else if (getOptions().strongCC()) {
        _impl.reset(new llvmdg::StrongControlClosure(_module, _options));
    } else if (getOptions().ntscdLegacyCD()) {
        // legacy NTSCD is ICFG always...
        _impl.reset(new llvmdg::legacy::NTSCD(_module, _options));
    } else if (getOptions().dodCD() || getOptions().dodRanganathCD() ||
               getOptions().dodntscdCD()) {
        // DOD on itself makes no sense, but allow it due to debugging
        if (icfg) {
            _impl.reset(
                    new llvmdg::InterproceduralDOD(_module, _options, pta, cg));
        } else {
            _impl.reset(new llvmdg::DOD(_module, _options));
        }
    } else {
        assert(false && "Unhandled analysis type");
        abort();
    }

    _interprocImpl.reset(
            new llvmdg::LLVMInterprocCD(_module, _options, pta, cg));
}

} // namespace dg
