#ifndef _DG_SPARSE_BITVECTOR_H_
#define _DG_SPARSE_BITVECTOR_H_

#include <cassert>
#include <cstdint>

#include "HashMap.h"
#include "Map.h"

namespace dg {
namespace ADT {

template <typename BitsT = uint64_t, typename ShiftT = uint64_t,
          typename IndexT = uint64_t, size_t SCALE = 1,
          typename BitsContainerT = dg::Map<ShiftT, BitsT>>
class SparseBitvectorImpl {
    // mapping from shift to bits
    BitsContainerT _bits{};

    static size_t _bitsNum() { return sizeof(BitsT) * 8; }
    static ShiftT _shift(IndexT i) { return i - (i % _bitsNum()); }

    static size_t _countBits(BitsT bits) {
        size_t num = 0;
        while (bits) {
            if (bits & 0x1)
                ++num;

            bits = bits >> 1;
        }

        return num;
    }

    void _addBit(IndexT i) {
        // for now we just push it back,
        // but we would rather do it somehow
        // more smartly (probably not using the vector)
        auto sft = _shift(i);
        _bits.emplace(sft, BitsT{1} << (i - sft));
    }

public:
    SparseBitvectorImpl() = default;
    SparseBitvectorImpl(IndexT i) { _addBit(i); } // singleton ctor

    SparseBitvectorImpl(const SparseBitvectorImpl&) = default;
    SparseBitvectorImpl(SparseBitvectorImpl&&) = default;

    void reset() { _bits.clear(); }
    bool empty() const { return _bits.empty(); }
    void swap(SparseBitvectorImpl& oth) { _bits.swap(oth._bits); }

    // TODO: use SFINAE to define empty body if a class without
    // reserve() is used...
    void reserve(size_t n) { _bits.reserve(n); }

    bool get(IndexT i) const {
        auto sft = _shift(i);
        assert(sft % _bitsNum() == 0);

        auto it = _bits.find(sft);
        if (it == _bits.end()) {
            return false;
        }

        return (it->second & (BitsT{1} << (i - sft)));
    }

    // returns the previous value of the i-th bit
    bool set(IndexT i) {
        auto sft = _shift(i);
        auto& B = _bits[sft];

        bool prev = B & (BitsT{1} << (i - sft));
        B |= BitsT{1} << (i - sft);

        return prev;
    }

    // union operation
    bool set(const SparseBitvectorImpl& rhs) {
        bool changed = false;
        for (auto& pair : rhs._bits) {
            auto& B = _bits[pair.first];
            auto old = B;
            B |= pair.second;
            changed |= old != B;
        }

        return changed;
    }

    // returns the previous value of the i-th bit
    bool unset(IndexT i) {
        auto sft = _shift(i);
        auto it = _bits.find(sft);
        if (it == _bits.end()) {
            return false;
        }

        // FIXME: use this implementation only for hash map
        // which returns read-only object and
        // modify directly it->second in other cases (for std::map)
        auto res = it->second & ~(BitsT{1} << (i - sft));
        if (res == 0) {
            _bits.erase(it);
        } else {
            _bits[sft] = res;
        }

        return true;
    }

    // FIXME: track the number of elements
    // in a variable, to avoid this search...
    size_t size() const {
        size_t num = 0;
        for (auto& it : _bits)
            num += _countBits(it.second);

        return num;
    }

    class const_iterator {
        typename BitsContainerT::const_iterator container_it;
        typename BitsContainerT::const_iterator container_end;
        size_t pos{0};

        const_iterator(const BitsContainerT& cont, bool end = false)
        :container_it(end ? cont.end() : cont.begin()),
         container_end(cont.end()) {
            // set-up the initial position
            if (!end && !cont.empty())
                _findClosestBit();
        }

        void _findClosestBit() {
            assert(pos < (sizeof(BitsT)*8));
            while (!(container_it->second & (BitsT{1} << pos))) {
                if (++pos == sizeof(BitsT)*8)
                    return;
            }
        }

        /*
         * This implementation seems less efficient,
         * but let's keep it commented here for
         * further experiments
        void _findClosestBit() {
            assert(pos < (sizeof(BitsT)*8));
            BitsT tmp = container_it->second;

            // shift tmp to start at position pos
            tmp >>= pos;

            if (tmp == 0) {
                pos = sizeof(BitsT)*8;
                assert(pos <= (sizeof(BitsT)*8));
                return;
            }

            int i = 0;
            while (tmp) {
                // pos bit set
                if (tmp & 0x1) {
                    pos += i;
                    assert(pos < (sizeof(BitsT)*8));
                    assert(container_it->second & (BitsT{1} << pos));
                    return;
                }

                if (tmp & 0xff) {
                    if (tmp & 0xf) {
                        // we tested this condition
                        // before entering this code
                        assert(!(tmp & 0x1));
                        if (tmp & 0x2) {
                            pos += i + 1;
                            assert(pos < (sizeof(BitsT)*8));
                            assert(container_it->second & (BitsT{1} << pos));
                            return;
                        } else {
                            assert(!(tmp & 0x3));
                            tmp >>= 2;
                            i += 2;
                        }
                    } else {
                        tmp >>= 4;
                        i += 4;
                    }
                } else {
                    tmp >>= 8;
                    i += 8;
                }
            }

            // not found
            pos = sizeof(BitsT)*8;
        }
        */

    public:
        const_iterator() = default;
        const_iterator& operator++() {
            // shift to the next bit in the current bits
            assert(pos < (sizeof(BitsT)*8));
            assert(container_it != container_end && "operator++ called on end");
            if (++pos != 64)
                _findClosestBit();

            if (pos == 64) {
                ++container_it;
                pos = 0;
                if (container_it != container_end) {
                    assert(container_it->second != 0 && "Empty bucket in a bitvector");
                    _findClosestBit();
                }
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        IndexT operator*() const {
            return container_it->first + pos;
        }

        bool operator==(const const_iterator& rhs) const {
            return pos == rhs.pos && container_it == rhs.container_it;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class SparseBitvectorImpl;
    };

    const_iterator begin() const { return const_iterator(_bits); }
    const_iterator end() const { return const_iterator(_bits, true /* end */); }

    friend class const_iterator;
};

using SparseBitvectorMapImpl = SparseBitvectorImpl<uint64_t, uint64_t, uint64_t, 1,
                                                   dg::Map<uint64_t, uint64_t>>;
using SparseBitvectorHashImpl = SparseBitvectorImpl<uint64_t, uint64_t, uint64_t, 1,
                                                    dg::HashMap<uint64_t, uint64_t>>;
using SparseBitvector = SparseBitvectorMapImpl;

} // namespace ADT
} // namespace dg

#endif // _DG_SPARSE_BITVECTOR_H_
