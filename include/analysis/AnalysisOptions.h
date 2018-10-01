#ifndef _DG_ANALYSIS_OPTIONS_H_
#define _DG_ANALYSIS_OPTIONS_H_

#include "Offset.h"

namespace dg {
namespace analysis {

struct AnalysisOptions {
    // Number of bytes in objects to track precisely
    Offset fieldSensitivity{Offset::UNKNOWN};

    AnalysisOptions& setFieldSensitivity(Offset o) {
        fieldSensitivity = o; return *this;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_OPTIONS_H_
