#ifndef DG_LLVM_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
#define DG_LLVM_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/llvm/LLVMAnalysisOptions.h"

namespace dg {

struct LLVMDataDependenceAnalysisOptions : public LLVMAnalysisOptions,
                                           DataDependenceAnalysisOptions {
    bool threads{false};

    LLVMDataDependenceAnalysisOptions() {
        // setup models for standard functions

        ///
        // Memory block functions
        ///
        // memcpy defines mem. pointed to by operand 0 from the offset 0
        // to the offset given by the operand 2
        functionModelAddDef("memcpy", {0, Offset(0), 2});
        functionModelAddUse("memcpy", {1, Offset(0), 2});
        functionModelAddDef("__memcpy_chk", {0, Offset(0), 2});
        functionModelAddUse("__memcpy_chk", {1, Offset(0), 2});
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
        functionModelAddUse("strlen", {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strchr", {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strrchr", {0, Offset(0), Offset::getUnknown()});

        functionModelAddDef("strcpy", {0, Offset(0), Offset::getUnknown()});
        functionModelAddUse("strcpy", {1, Offset(0), Offset::getUnknown()});
        functionModelAddDef("strncpy", {0, Offset(0), 2});
        functionModelAddUse("strncpy", {1, Offset(0), 2});
    };
};

} // namespace dg

#endif // DG_LLVM_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
