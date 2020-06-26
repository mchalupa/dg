#ifndef DG_LLVM_CDA_OPTIONS_H_
#define DG_LLVM_CDA_OPTIONS_H_

#include "dg/llvm/LLVMAnalysisOptions.h"
#include "dg/ControlDependence/ControlDependenceAnalysisOptions.h"

namespace dg {

struct LLVMControlDependenceAnalysisOptions :
    public LLVMAnalysisOptions, ControlDependenceAnalysisOptions
{
    bool _nodePerInstruction{false};

    void setNodePerInstruction(bool b) { _nodePerInstruction = b; }
    bool nodePerInstruction() const { return _nodePerInstruction; }
};

} // namespace dg

#endif
