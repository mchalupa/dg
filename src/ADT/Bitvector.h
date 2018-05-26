#ifndef _DG_SPARSE_BITVECTOR_H_
#define _DG_SPARSE_BITVECTOR_H_

#include <vector>
#include <cassert>

namespace dg {
namespace ADT {

namespace detail {
template <typename InnerT = uint64_t,
          typename ShiftT = uint64_t>
class Bits {
    InnerT _bits{0};
    ShiftT _shift;

public:
    Bits(ShiftT shift): _shift(shift) {}

    static size_t bitsNum() { return sizeof(InnerT) * 8; }
    bool operator==(const Bits& rhs) const { return _bits == rhs._bits && _shift == rhs._shift; }
    bool operator!=(const Bits& rhs) const { return !operator==(rhs); }

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
        const Bits *bits{nullptr};
        size_t pos{0};

        bool isEnd() const {
            return pos == Bits::bitsNum();
        }

        void _findNext() {
            assert(pos < Bits::bitsNum());
            while (!(bits->_bits & (static_cast<InnerT>(1) << pos))) {
                ++pos;

                if (isEnd())
                    return;
            }
        }

        const_iterator(const Bits *bits, size_t pos = 0)
        :bits(bits), pos(pos) {
            assert(bits && "No bits given");
            // start at the first element
            if (!isEnd() && !(bits->_bits & 1))
                operator++();
        }

    public:
        const_iterator() = default;
        const_iterator(const const_iterator&) = default;
        const_iterator& operator=(const const_iterator&) = default;

        const_iterator& operator++() {
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
            assert(bits->mayContain(bits->_shift + pos));
            return bits->_shift + pos;
        }

        bool operator==(const const_iterator& rhs) const {
            return pos == rhs.pos && bits == rhs.bits;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class Bits;
    };

    const_iterator begin() const { return const_iterator(this); }
    const_iterator end() const { return const_iterator(this, bitsNum()); }

    friend class const_iterator;
};

} // namespace detail

template <typename BitsT = detail::Bits<uint64_t, uint64_t>,
          size_t SCALE = 1>
class SparseBitvectorImpl {
    using BitsContainerT = std::vector<BitsT>;
    BitsContainerT _bits{};

    // get the element from _bits that may contain i
    // or null if there's no such element
    const BitsT *_getBits(size_t i) const {
        for (const BitsT& elem : _bits) {
            if (elem.mayContain(i))
                return &elem;
        }

        return nullptr;
    }

    BitsT *_getBits(size_t i) {
        for (BitsT& elem : _bits) {
            if (elem.mayContain(i))
                return &elem;
        }


        return nullptr;
    }

    void _addBits(size_t i) {
        // for now we just push it back,
        // but we would rather do it somehow
        // more smartly (probably not using the vector)
        _bits.emplace_back(i - (i % BitsT::bitsNum()));
        _bits.back().set(i);
    }

public:
    SparseBitvectorImpl() = default;
    SparseBitvectorImpl(size_t i) { set(i); } // singleton ctor

    SparseBitvectorImpl(const SparseBitvectorImpl&) = default;
    SparseBitvectorImpl(SparseBitvectorImpl&&) = default;

    void reset() {
        _bits.clear();
    }

    bool empty() const {
        return _bits.empty();
    }

    bool get(size_t i) const {
        const BitsT *B = _getBits(i);
        return B ? B->get(i) : false;
    }

    // returns the previous value of the i-th bit
    bool set(size_t i) {
        if (BitsT *B = _getBits(i)) {
            return B->set(i);
        } else {
            _addBits(i);
            return false;
        }
    }

    // this is the union operation
    bool merge(const SparseBitvectorImpl& rhs) {
        bool changed = false;
        // FIXME: this is inefficient
        for (size_t i : rhs) {
            // if we changed a bit from false to true,
            // we changed the bitvector
            changed |= (set(i) == false);
        }

        return changed;
    }

    size_t size() const {
        size_t num = 0;
        for (auto& elem : _bits)
            num += elem.size();

        return num;
    }

    class const_iterator {
        const BitsContainerT *container{nullptr};
        size_t pos{0};
        typename BitsT::const_iterator innerIt{};

        const_iterator(const BitsContainerT& cont, size_t pos = 0)
        :container(&cont), pos(pos) {
            if (!cont.empty())
                innerIt = cont.front().begin();
        }
    public:
        const_iterator() = default;

        const_iterator& operator++() {
            ++innerIt;
            if (innerIt == (*container)[pos].end()) {
                ++pos;
                if (pos < container->size())
                    innerIt = (*container)[pos].begin();
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        size_t operator*() const {
            return *innerIt;
        }

        bool operator==(const const_iterator& rhs) const {
            return pos == rhs.pos && (container == rhs.container);
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class SparseBitvectorImpl;
    };

    const_iterator begin() const { return const_iterator(_bits); }
    const_iterator end() const { return const_iterator(_bits, _bits.size()); }

    friend class const_iterator;
};

using SparseBitvector = SparseBitvectorImpl<detail::Bits<uint64_t, uint64_t>, 1>;

} // namespace ADT
} // namespace dg

#endif // _DG_SPARSE_BITVECTOR_H_
