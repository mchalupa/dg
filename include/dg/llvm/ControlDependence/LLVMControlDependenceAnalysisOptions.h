#ifndef DG_LLVM_CDA_OPTIONS_H_
#define DG_LLVM_CDA_OPTIONS_H_

#include "dg/ControlDependence/ControlDependenceAnalysisOptions.h"
#include "dg/llvm/LLVMAnalysisOptions.h"

namespace dg {

struct LLVMControlDependenceAnalysisOptions : public LLVMAnalysisOptions,
                                              ControlDependenceAnalysisOptions {
    bool _nodePerInstruction{false};
    bool _icfg{false};

    void setNodePerInstruction(bool b) { _nodePerInstruction = b; }
    bool nodePerInstruction() const { return _nodePerInstruction; }
    bool ICFG() const { return _icfg; }
};

} // namespace dg

#endif
