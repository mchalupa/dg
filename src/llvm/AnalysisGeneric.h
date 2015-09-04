#ifndef _LLVM_ANALYSIS_GENERIC_H_
#define _LLVM_ANALYSIS_GENERIC_H_

#include "PointsTo.h"

namespace llvm {
    class ConstantExpr;
    class DataLayout;
};

namespace dg {

class LLVMDependenceGraph;

namespace analysis {

Pointer getConstantExprPointer(const llvm::ConstantExpr *CE,
                               LLVMDependenceGraph *dg,
                               const llvm::DataLayout *DL);

} // namespace analysis
} // namespace dg

#endif // _LLVM_ANALYSIS_GENERIC_H_
