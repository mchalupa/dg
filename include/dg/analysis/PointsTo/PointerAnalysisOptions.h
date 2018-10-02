#ifndef _DG_POINTER_ANALYSIS_OPTIONS_H_
#define _DG_POINTER_ANALYSIS_OPTIONS_H_

#include "dg/analysis/AnalysisOptions.h"

namespace dg {
namespace analysis {

struct PointerAnalysisOptions : AnalysisOptions {
    // Preprocess GEP nodes such that the offset
    // is directly set to UNKNOWN if we can identify
    // that it will be the result of the computation
    // (saves iterations)
    bool preprocessGeps{true};

    // Should the analysis keep track of invalidate
    // (e.g. freed) memory? Pointers pointing to such
    // memory are then represented as pointing to
    // INVALIDATED object.
    bool invalidateNodes{false};

    PointerAnalysisOptions& setInvalidateNodes(bool b) { invalidateNodes = b; return *this;}
    PointerAnalysisOptions& setPreprocessGeps(bool b)  { preprocessGeps = b; return *this;}
};

} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_ANALYSIS_OPTIONS_H_
