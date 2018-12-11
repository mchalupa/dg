#ifndef _DG_SPARSE_BITVECTOR_H_
#define _DG_SPARSE_BITVECTOR_H_

#include <map>
#include <cassert>

namespace dg {
namespace ADT {

template <typename BitsT = uint64_t, typename ShiftT = uint64_t, size_t SCALE = 1>
class SparseBitvectorImpl {
    // mapping from shift to bits
    using BitsContainerT = std::map<ShiftT, BitsT>;
    BitsContainerT _bits{};

    static size_t _bitsNum() { return sizeof(BitsT) * 8; }
    static ShiftT _shift(size_t i) { return i - (i % _bitsNum()); }

    static size_t _countBits(BitsT bits) {
        size_t num = 0;
        while (bits) {
            if (bits & 0x1)
                ++num;

            bits = bits >> 1;
        }

        return num;
    }

    void _addBits(size_t i) {
        // for now we just push it back,
        // but we would rather do it somehow
        // more smartly (probably not using the vector)
        auto sft = _shift(i);
        _bits.emplace(sft, (1UL << (i - sft)));
    }

public:
    SparseBitvectorImpl() = default;
    SparseBitvectorImpl(size_t i) { set(i); } // singleton ctor

    SparseBitvectorImpl(const SparseBitvectorImpl&) = default;
    SparseBitvectorImpl(SparseBitvectorImpl&&) = default;

    void reset() { _bits.clear(); }
    bool empty() const { return _bits.empty(); }
    void swap(SparseBitvectorImpl& oth) { _bits.swap(oth._bits); }

    bool get(size_t i) const {
        auto sft = _shift(i);
        assert(sft % _bitsNum() == 0);

        auto it = _bits.find(sft);
        if (it == _bits.end()) {
            return false;
        }

        return (it->second & (1UL << (i - sft)));
    }

    // returns the previous value of the i-th bit
    bool set(size_t i) {
        auto sft = _shift(i);
        auto it = _bits.find(sft);
        if (it == _bits.end()) {
            _addBits(i);
            return false;
        }

        bool prev = (it->second & (1UL << (i - sft)));
        it->second |= (1UL << (i - sft));

        return prev;
    }

    // union operation
    bool set(const SparseBitvectorImpl& rhs) {
        bool changed = false;
        for (auto& pair : rhs._bits) {
            auto& B = _bits[pair.first];
            auto old = B;
            B |= pair.second;
            changed |= (old != B);
        }

        return changed;
    }

    // returns the previous value of the i-th bit
    bool unset(size_t i) {
        auto sft = _shift(i);
        auto it = _bits.find(sft);
        if (it == _bits.end()) {
            return false;
        }

        it->second &= ~(1UL << (i - sft));
        if (it->second == 0) {
            _bits.erase(it);
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
            while (!(container_it->second & (1UL << pos))) {
                if (++pos == sizeof(BitsT)*8)
                    return;
            }
        }

    public:
        const_iterator() = default;
        const_iterator& operator++() {
            // shift to the next bit in the current bits
            assert(pos < (sizeof(BitsT)*8));
            if (++pos != 64)
                _findClosestBit();

            if (pos == 64) {
                ++container_it;
                pos = 0;
                if (container_it != container_end) {
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

        size_t operator*() const {
            return container_it->first + pos;
        }

        bool operator==(const const_iterator& rhs) const {
            return pos == rhs.pos && (container_it == rhs.container_it);
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

using SparseBitvector = SparseBitvectorImpl<uint64_t, uint64_t, 1>;

} // namespace ADT
} // namespace dg

#endif // _DG_SPARSE_BITVECTOR_H_
