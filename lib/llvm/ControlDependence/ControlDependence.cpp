#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "llvm/ControlDependence/legacy/NTSCD.h"
#include "llvm/ControlDependence/SCD.h"
#include "llvm/ControlDependence/InterproceduralCD.h"

namespace dg {

void LLVMControlDependenceAnalysis::initializeImpl() {
    if (getOptions().standardCD()) {
        _impl.reset(new llvmdg::SCD(_module, _options));
    } else if (getOptions().ntscdCD()) {
        _impl.reset(new llvmdg::legacy::NTSCD(_module, _options));
    } else {
        assert(false && "Unhandled analysis type");
        abort();
    }

    _interprocImpl.reset(new llvmdg::LLVMInterprocCD(_module, _options));
}

} // namespace dg
