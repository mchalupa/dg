#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_

#include "analysis/Offset.h"
#include "analysis/AnalysisOptions.h"

namespace dg {
namespace analysis {

struct ReachingDefinitionsAnalysisOptions : AnalysisOptions {
    // Should we perform strong update with unknown memory?
    // NOTE: not sound.
    bool strongUpdateUnknown{false};

    // Undefined functions have no side-effects
    bool undefinedArePure{false};

    // Maximal size of the reaching definitions set.
    // If this size is exceeded, the set is cropped to unknown.
    Offset maxSetSize{Offset::UNKNOWN};

    // Should we perform sparse or dense analysis?
    bool sparse{false};

    // Does the analysis track concrete bytes
    // or just objects?
    bool fieldInsensitive{false};


    ReachingDefinitionsAnalysisOptions& setStrongUpdateUnknown(bool b) {
        strongUpdateUnknown = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setUndefinedArePure(bool b) {
        undefinedArePure = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setMaxSetSize(Offset s) {
        maxSetSize = s; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setSparse(bool b) {
        sparse = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setFieldInsensitive(bool b) {
        fieldInsensitive = b; return *this;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_REACHING_ANALYSIS_OPTIONS_H_
