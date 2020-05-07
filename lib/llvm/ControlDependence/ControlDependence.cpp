#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "llvm/ControlDependence/NTSCD.h"

namespace dg {

void LLVMControlDependenceAnalysis::initializeImpl() {
    if (getOptions().standardCD()) {
        assert(false && "Unhandled analysis type");
    } else if (getOptions().ntscdCD()) {
        _impl.reset(new llvmdg::NTSCD(_module, _options));
    } else {
        assert(false && "Unhandled analysis type");
        abort();
    }
}

} // namespace dg
