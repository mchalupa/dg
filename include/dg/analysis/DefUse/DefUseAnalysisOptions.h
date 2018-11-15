#ifndef _DG_DEF_USE_ANALYSIS_OPTIONS_H_
#define _DG_DEF_USE_ANALYSIS_OPTIONS_H_

#include "dg/analysis/AnalysisOptions.h"

namespace dg {
namespace analysis {

struct DefUseAnalysisOptions : AnalysisOptions {
    bool undefinedArePure{false};
};

} // namespace analysis
} // namespace dg

#endif // _DG_DEF_USE_ANALYSIS_OPTIONS_H_
