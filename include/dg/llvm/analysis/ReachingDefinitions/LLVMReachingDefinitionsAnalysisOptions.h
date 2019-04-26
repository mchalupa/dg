#ifndef _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
#define _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_

#include "dg/llvm/analysis/LLVMAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"

namespace dg {
namespace analysis {

struct LLVMReachingDefinitionsAnalysisOptions :
    public LLVMAnalysisOptions, ReachingDefinitionsAnalysisOptions
{
    enum class AnalysisType { dataflow, ssa } analysisType{AnalysisType::dataflow};

    bool threads{false};
    bool isDataFlow() const { return analysisType == AnalysisType::dataflow; }
    bool isSSA() const { return analysisType == AnalysisType::ssa; }

    LLVMReachingDefinitionsAnalysisOptions() {
        // setup models for standard functions

        ///
        // Memory block functions
        ///
        // memcpy defines mem. pointed to by operand 0 from the offset 0
        // to the offset given by the operand 2
        functionModelAddDef("memcpy", {0, Offset(0), 2});
        functionModelAddUse("memcpy", {1, Offset(0), 2});
        functionModelAddDef("llvm.memcpy.p0i8.p0i8.i64", {0, Offset(0), 2});
        functionModelAddUse("llvm.memcpy.p0i8.p0i8.i64", {1, Offset(0), 2});
        functionModelAddDef("llvm.memcpy.p0i8.p0i8.i32", {0, Offset(0), 2});
        functionModelAddUse("llvm.memcpy.p0i8.p0i8.i32", {1, Offset(0), 2});

        functionModelAddDef("memmove", {0, Offset(0), 2});
        functionModelAddUse("memmove", {1, Offset(0), 2});

        functionModelAddDef("memset", {0, Offset(0), 2});

        functionModelAddUse("memcmp", {0, Offset(0), 2});
        functionModelAddUse("memcmp", {1, Offset(0), 2});

        ///
        // String handling functions
        ///
        functionModelAddUse("strlen",  {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strchr",  {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strrchr", {0, Offset(0), Offset::getUnknown()});

        functionModelAddDef("strcpy", {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strcpy", {1, Offset(0), Offset::getUnknown()});
        functionModelAddDef("strncpy", {0, Offset(0), 2});
        functionModelAddUse("strncpy", {1, Offset(0), 2});
    };
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
