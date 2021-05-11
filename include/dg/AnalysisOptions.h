#ifndef DG_ANALYSIS_OPTIONS_H_
#define DG_ANALYSIS_OPTIONS_H_

#include <cassert>
#include <map>
#include <string>

#include "Offset.h"

namespace dg {

///
// Enumeration for functions that are known to
// return freshly allocated memory.
enum class AllocationFunction {
    NONE,    // not an allocation function
    MALLOC,  // function behaves like malloc
    CALLOC,  // function behaves like calloc
    ALLOCA,  // function behaves like alloca
    REALLOC, // function behaves like realloc
    MALLOC0, // function behaves like malloc,
             // but cannot return NULL
    CALLOC0, // function behaves like calloc,
             // but cannot return NULL
};

struct AnalysisOptions {
    // Number of bytes in objects to track precisely
    Offset fieldSensitivity{Offset::UNKNOWN};

    AnalysisOptions &setFieldSensitivity(Offset o) {
        fieldSensitivity = o;
        return *this;
    }

    std::map<const std::string, AllocationFunction> allocationFunctions = {
            {"malloc", AllocationFunction::MALLOC},
            {"calloc", AllocationFunction::CALLOC},
            {"alloca", AllocationFunction::ALLOCA},
            {"realloc", AllocationFunction::REALLOC},
    };

    void addAllocationFunction(const std::string &name, AllocationFunction F) {
#ifndef NDEBUG
        auto ret =
#endif
                allocationFunctions.emplace(name, F);
        assert(ret.second && "Already have this allocation function");
    }

    AllocationFunction getAllocationFunction(const std::string &name) const {
        auto it = allocationFunctions.find(name);
        if (it == allocationFunctions.end())
            return AllocationFunction::NONE;
        return it->second;
    }

    bool isAllocationFunction(const std::string &name) const {
        return getAllocationFunction(name) != AllocationFunction::NONE;
    }
};

} // namespace dg

#endif
