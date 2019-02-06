#include <set>
#include <cassert>
#include <cstdint>

#include "dg/ADT/NumberSet.h"

using namespace dg::ADT;

extern "C"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::set<uint64_t> S;
    BitvectorNumberSet B;

    const auto elems = size / (sizeof(uint64_t));
    const uint64_t *numbers = reinterpret_cast<const uint64_t *>(data);
    for (unsigned i = 0; i < elems; ++i) {
        B.add(numbers[i]);
        S.insert(numbers[i]);
    }

    for (auto x : S) {
        assert(B.has(x));
    }

    for (unsigned int i = 0; i < elems; ++i) {
        if (S.count(i) > 0)
            assert(B.has(i));
        else
            assert(!B.has(i));
    }

    return 0;
}

