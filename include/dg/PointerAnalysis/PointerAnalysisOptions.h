#ifndef DG_POINTER_ANALYSIS_OPTIONS_H_
#define DG_POINTER_ANALYSIS_OPTIONS_H_

#include "dg/AnalysisOptions.h"

namespace dg {

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

    PointerAnalysisOptions &setInvalidateNodes(bool b) {
        invalidateNodes = b;
        return *this;
    }
    PointerAnalysisOptions &setPreprocessGeps(bool b) {
        preprocessGeps = b;
        return *this;
    }

    // Perform maximally this number of iterations.
    // If exceeded, the analysis is terminated and points-to sets
    // of the unprocessed nodes are set to {}.
    size_t maxIterations{0};
};

} // namespace dg

#endif
