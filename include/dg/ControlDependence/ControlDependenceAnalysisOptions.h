#ifndef DG_CDA_OPTIONS_H_
#define DG_CDA_OPTIONS_H_

#include "dg/AnalysisOptions.h"

namespace dg {

struct ControlDependenceAnalysisOptions : AnalysisOptions {
    // FIXME: add options class for CD
    enum class CDAlgorithm {
        STANDARD,
        LEGACY_NTSCD,
        NTSCD2,
        NTSCD
    } algorithm;

    // take into account interprocedural control dependencies
    // (raising e.g., from calls to exit() which terminates the program)
    bool interprocedural{true};

    bool standardCD() const { return algorithm == CDAlgorithm::STANDARD; }
    bool ntscdCD() const { return algorithm == CDAlgorithm::NTSCD; }
    bool ntscd2CD() const { return algorithm == CDAlgorithm::NTSCD2; }
    bool legacyNtscdCD() const { return algorithm == CDAlgorithm::LEGACY_NTSCD; }
    bool interproceduralCD() const { return interprocedural; }
};

} // namespace dg
#endif
