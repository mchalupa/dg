#ifndef _DG_SPARSE_BITS_H_
#define _DG_SPARSE_BITS_H_

#include <cassert>
#include <cstddef> // size_t
#include <cstdint>

namespace dg {
namespace ADT {

// an element that holds a sequence of bits (up to 64 bits usually)
// along with offset. So this class can represent a sequence
// of [S, S(+sizeof(InnerT)*8)] bits. (E.g. 100th, 101th, ..., 163th bit)
template <typename InnerT = uint64_t, typename ShiftT = uint64_t>
class ShiftedBits {
    InnerT _bits{0};
    ShiftT _shift;

  public:
    ShiftedBits(ShiftT shift) : _shift(shift) {}

    static size_t bitsNum() { return sizeof(InnerT) * 8; }
    bool operator==(const ShiftedBits &rhs) const {
        return _bits == rhs._bits && _shift == rhs._shift;
    }
    bool operator!=(const ShiftedBits &rhs) const { return !operator==(rhs); }

    bool mayContain(size_t i) const {
        return i >= _shift && i - _shift < bitsNum();
    }

    size_t size() const {
        size_t num = 0;
        for (size_t i = 0; i < bitsNum(); ++i)
            if (_bits & (static_cast<InnerT>(1) << i))
                ++num;

        return num;
    }

    bool empty() const { return _bits == 0; }

    bool get(size_t i) const {
        if (!mayContain(i))
            return false;

        assert(i - _shift < bitsNum());
        return _bits & (static_cast<InnerT>(1) << (i - _shift));
    }

    // return the previous value
    bool set(size_t i) {
        assert(mayContain(i));
        bool ret = get(i);
        _bits |= (static_cast<InnerT>(1) << (i - _shift));
        return ret;
    }

    class const_iterator {
        const ShiftedBits *bits{nullptr};
        size_t pos{0};

        bool isEnd() const { return pos == ShiftedBits::bitsNum(); }

        void _findNext() {
            assert(pos < ShiftedBits::bitsNum());
            while (!(bits->_bits & (static_cast<InnerT>(1) << pos))) {
                ++pos;

                if (isEnd())
                    return;
            }
        }

        const_iterator(const ShiftedBits *bits, size_t pos = 0)
                : bits(bits), pos(pos) {
            assert(bits && "No bits given");
            // start at the first element
            if (!isEnd() && !(bits->_bits & 1))
                operator++();
        }

      public:
        const_iterator() = default;
        const_iterator(const const_iterator &) = default;
        const_iterator &operator=(const const_iterator &) = default;

        const_iterator &operator++() {
            ++pos;
            if (!isEnd())
                _findNext();
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        size_t operator*() const {
            assert(pos < ShiftedBits::bitsNum());
            assert(bits->mayContain(bits->_shift + pos));
            return bits->_shift + pos;
        }

        bool operator==(const const_iterator &rhs) const {
            return pos == rhs.pos && bits == rhs.bits;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class ShiftedBits;
    };

    const_iterator begin() const { return const_iterator(this); }
    const_iterator end() const { return const_iterator(this, bitsNum()); }

    friend class const_iterator;
};

// a set of bits with 0 offset. This is a special case
// of ShiftedBits with _shift = 0
template <typename InnerT = uint64_t>
class Bits {
    InnerT _bits{0};

  public:
    static size_t bitsNum() { return sizeof(InnerT) * 8; }
    bool operator==(const Bits &rhs) const { return _bits == rhs._bits; }
    bool operator!=(const Bits &rhs) const { return !operator==(rhs); }

    bool empty() const { return _bits == 0; }
    bool mayContain(size_t i) const { return i < bitsNum(); }

    size_t size() const {
        size_t num = 0;
        for (size_t i = 0; i < bitsNum(); ++i)
            if (_bits & (static_cast<InnerT>(1) << i))
                ++num;

        return num;
    }

    bool get(size_t i) const {
        if (!mayContain(i))
            return false;

        assert(i < bitsNum());
        return _bits & (static_cast<InnerT>(1) << i);
    }

    // return the previous value
    bool set(size_t i) {
        assert(mayContain(i));
        bool ret = get(i);
        _bits |= (static_cast<InnerT>(1) << i);
        return ret;
    }

    class const_iterator {
        const Bits *bits{nullptr};
        size_t pos{0};

        bool isEnd() const { return pos == Bits::bitsNum(); }

        void _findNext() {
            assert(pos < Bits::bitsNum());
            while (!(bits->_bits & (static_cast<InnerT>(1) << pos))) {
                ++pos;

                if (isEnd())
                    return;
            }
        }

        const_iterator(const Bits *bits, size_t pos = 0)
                : bits(bits), pos(pos) {
            assert(bits && "No bits given");
            // start at the first element
            if (!isEnd() && !(bits->_bits & 1))
                operator++();
        }

      public:
        const_iterator() = default;
        const_iterator(const const_iterator &) = default;
        const_iterator &operator=(const const_iterator &) = default;

        const_iterator &operator++() {
            ++pos;
            if (!isEnd())
                _findNext();
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        size_t operator*() const {
            assert(pos < Bits::bitsNum());
            assert(bits->mayContain(pos));
            return pos;
        }

        bool operator==(const const_iterator &rhs) const {
            return pos == rhs.pos && bits == rhs.bits;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class Bits;
    };

    const_iterator begin() const { return const_iterator(this); }
    const_iterator end() const { return const_iterator(this, bitsNum()); }

    friend class const_iterator;
};

} // namespace ADT
} // namespace dg

#endif // _DG_SPARSE_BITS_H_
