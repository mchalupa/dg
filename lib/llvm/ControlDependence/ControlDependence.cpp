#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "llvm/ControlDependence/ControlClosure.h"
#include "llvm/ControlDependence/legacy/NTSCD.h"
#include "llvm/ControlDependence/DOD.h"
#include "llvm/ControlDependence/NTSCD.h"
#include "llvm/ControlDependence/SCD.h"
#include "llvm/ControlDependence/InterproceduralCD.h"

namespace dg {

void LLVMControlDependenceAnalysis::initializeImpl() {
    if (getOptions().standardCD()) {
        _impl.reset(new llvmdg::SCD(_module, _options));
    } else if (getOptions().ntscdCD() || getOptions().ntscd2CD() ||
               getOptions().ntscdRanganathCD()) {
        _impl.reset(new llvmdg::NTSCD(_module, _options));
    } else if (getOptions().strongCC()) {
        _impl.reset(new llvmdg::StrongControlClosure(_module, _options));
    } else if (getOptions().ntscdLegacyCD()) {
        _impl.reset(new llvmdg::legacy::NTSCD(_module, _options));
    } else if (getOptions().dodCD() || getOptions().dodRanganathCD() ||
               getOptions().dodntscdCD()) {
        // DOD on itself makes no sense, but allow it due to debugging
        _impl.reset(new llvmdg::DOD(_module, _options));
    } else {
        assert(false && "Unhandled analysis type");
        abort();
    }

    _interprocImpl.reset(new llvmdg::LLVMInterprocCD(_module, _options));
}

} // namespace dg
