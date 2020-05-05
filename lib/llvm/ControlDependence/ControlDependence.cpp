#include "dg/llvm/ControlDependence/ControlDependence.h"

namespace dg {

void LLVMControlDependenceAnalysis::run() {
    if (getOptions().standardCD()) {
        assert(false && "Unhandled analysis type");
    } else if (getOptions().ntscdCD()) {
        assert(false && "Unhandled analysis type");
    } else {
        assert(false && "Unhandled analysis type");
        abort();
    }
}

} // namespace dg
