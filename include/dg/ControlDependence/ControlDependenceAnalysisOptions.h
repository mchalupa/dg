#ifndef DG_CDA_OPTIONS_H_
#define DG_CDA_OPTIONS_H_

#include "dg/AnalysisOptions.h"

namespace dg {

struct ControlDependenceAnalysisOptions : AnalysisOptions {
    // FIXME: add options class for CD
    enum class CDAlgorithm {
        STANDARD,
        NTSCD_LEGACY,
        NTSCD2,
        NTSCD_RANGANATH,      // fixed version of Ranganath's alg.
        NTSCD_RANGANATH_ORIG, // original (wrong) version of Ranaganath's alg.
        NTSCD,
        DOD_RANGANATH,
        DOD,
        DODNTSCD, // DOD + NTSCD
        STRONG_CC
    } algorithm;

    // take into account interprocedural control dependencies
    // (raising e.g., from calls to exit() which terminates the program)
    bool interprocedural{true};

    bool standardCD() const { return algorithm == CDAlgorithm::STANDARD; }
    bool ntscdCD() const { return algorithm == CDAlgorithm::NTSCD; }
    bool ntscd2CD() const { return algorithm == CDAlgorithm::NTSCD2; }
    bool ntscdRanganathCD() const {
        return algorithm == CDAlgorithm::NTSCD_RANGANATH;
    }
    bool ntscdRanganathOrigCD() const {
        return algorithm == CDAlgorithm::NTSCD_RANGANATH_ORIG;
    }
    bool ntscdLegacyCD() const {
        return algorithm == CDAlgorithm::NTSCD_LEGACY;
    }
    bool dodRanganathCD() const {
        return algorithm == CDAlgorithm::DOD_RANGANATH;
    }
    bool dodCD() const { return algorithm == CDAlgorithm::DOD; }
    bool dodntscdCD() const { return algorithm == CDAlgorithm::DODNTSCD; }
    bool strongCC() const { return algorithm == CDAlgorithm::STRONG_CC; }
    bool interproceduralCD() const { return interprocedural; }

    ///
    // Return true if the computed control dependencies
    // contain NTSCD dependencies
    bool isNonterminationSensitive() const {
        // DOD is for infinite loops, but it is not what we
        // want when asking for non-termination sensitive...
        return !standardCD() && !dodCD() && !dodRanganathCD();
    }
};

} // namespace dg
#endif
