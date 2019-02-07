#include <set>
#include <cassert>
#include <cstdint>

#include "dg/analysis/ReachingDefinitions/DisjunctiveIntervalMap.h"

extern "C"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    dg::analysis::rd::DisjunctiveIntervalMap<int, int> M;

    const auto elems = size / sizeof(int);
    if (elems == 0)
        return 0;

    const int *numbers = reinterpret_cast<const int *>(data);
    for (unsigned i = 0; i < elems - 1; i += 2) {
        int a = numbers[i], b = numbers[i+1];
        if (a > b)
            std::swap(a, b);
        assert(a <= b);
        M.add(a, b, 0);
    }

    return 0;
}

