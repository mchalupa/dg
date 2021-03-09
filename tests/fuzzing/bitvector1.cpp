#undef NDEBUG

#include <cassert>
#include <cstdint>
#include <set>

#include "dg/ADT/Bitvector.h"

extern "C"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::set<uint64_t> S;
    dg::ADT::SparseBitvector B;

    const auto elems = size / sizeof(uint64_t);
    const uint64_t *numbers = reinterpret_cast<const uint64_t *>(data);
    for (unsigned i = 0; i < elems; ++i) {
        B.set(numbers[i]);
        S.insert(numbers[i]);
    }

    for (auto x : S)
        assert(B.get(x));

    for (unsigned int i = 0; i < elems; ++i) {
        if (S.count(i) > 0)
            assert(B.get(i));
        else
            assert(!B.get(i));
    }

    for (auto x : S)
        assert(B.unset(x));

    for (auto x : S)
        assert(!B.get(x));

    return 0;
}
