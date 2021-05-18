#ifndef _DG_NUMBER_SET_H_
#define _DG_NUMBER_SET_H_

#include "Bits.h"
#include "Bitvector.h"

namespace dg {
namespace ADT {

// this is just a wrapper around sparse bitvector
// that translates the bitvector methods to a new methods.
// There is no possibility to remove elements from the set.
class BitvectorNumberSet {
    using NumT = uint64_t;
    using ContainerT = SparseBitvectorImpl<uint64_t, NumT>;

    ContainerT _bitvector;

  public:
    using const_iterator = typename ContainerT::const_iterator;

    BitvectorNumberSet() = default;
    BitvectorNumberSet(size_t n) : _bitvector(n){};
    BitvectorNumberSet(BitvectorNumberSet &&) = default;

    bool add(NumT n) { return !_bitvector.set(n); }
    bool has(NumT n) const { return _bitvector.get(n); }
    bool empty() const { return _bitvector.empty(); }
    size_t size() const { return _bitvector.size(); }
    void swap(BitvectorNumberSet &oth) { oth._bitvector.swap(_bitvector); }

    const_iterator begin() const { return _bitvector.begin(); }
    const_iterator end() const { return _bitvector.end(); }
};

// This class is a container for a set of numbers
// that is optimized for holding small values
// (values less than sizeof(NumT)*8*SmallElemNum)).
// If a greater value is inserted, the whole container
// is lifted to a normal set.
// There is no possibility to remove elements from the set.
class SmallNumberSet {
    using NumT = uint64_t;
    // NOTE: if we change this to something that
    // has non-trivial ctors/dtors, we must reflect it in the code!
    using SmallSetT = Bits<NumT>;
    using BigSetT = BitvectorNumberSet;

    bool is_small{true};

    union SetT {
        SmallSetT small;
        BigSetT big;

        SetT() : small() {}
        ~SetT() {}
    } _set;

    void _lift(NumT n) {
        assert(is_small);

        auto S = BigSetT(n);
        for (auto x : _set.small)
            S.add(x);

        // initialize the big-set
        new (&_set.big) BigSetT();
        S.swap(_set.big);

        is_small = false;
    }

  public:
    ~SmallNumberSet() {
        // if we used the big set, call its destructor
        // (for smalls set there's nothing to do)
        if (!is_small)
            _set.big.~BigSetT();
    }

    bool add(NumT n) {
        if (is_small) {
            if (_set.small.mayContain(n))
                return !_set.small.set(n);
            _lift(n);
            return true;

        } else
            return _set.big.add(n);
    }

    bool has(NumT n) const {
        return is_small ? _set.small.get(n) : _set.big.has(n);
    }

    bool empty() const {
        return is_small ? _set.small.empty() : _set.big.empty();
    }

    size_t size() const {
        return is_small ? _set.small.size() : _set.big.size();
    }

    class const_iterator {
        const bool is_small;

        union ItT {
            SmallSetT::const_iterator small_it;
            BigSetT::const_iterator big_it;
            ItT() {}
            ~ItT() {}
        } _it;

      public:
        const_iterator(const SmallNumberSet &S, bool end = false)
                : is_small(S.is_small) {
            if (is_small) {
                if (end)
                    _it.small_it = S._set.small.end();
                else
                    _it.small_it = S._set.small.begin();
            } else {
                if (end)
                    _it.big_it = S._set.big.end();
                else
                    _it.big_it = S._set.big.begin();
            }
        }

        const_iterator(const const_iterator &) = default;

        const_iterator &operator++() {
            if (is_small)
                ++_it.small_it;
            else
                ++_it.big_it;

            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        size_t operator*() const {
            if (is_small)
                return *_it.small_it;
            return *_it.big_it;
        }

        bool operator==(const const_iterator &rhs) const {
            if (is_small != rhs.is_small)
                return false;

            if (is_small)
                return _it.small_it == rhs._it.small_it;
            return _it.big_it == rhs._it.big_it;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }
    };

    const_iterator begin() const { return const_iterator(*this); }
    const_iterator end() const { return const_iterator(*this, true /* end */); }

    friend class const_iterator;
};

#if 0
// This class is a container for a set of numbers
// that is optimized for holding small values
// (values less than sizeof(NumT)*8*SmallElemNum)).
// If a greater value is inserted, the whole container
// is lifted to a normal set.
// There is no possibility to remove elements from the set.
template <typename NumT = uint64_t,
          typename BigSetT = BitvectorNumberSet,
          size_t SmallElemNum = 1>
class SmallNumberSet {
    NumT[SmallElemNum] _smallNums{};
    std::unique_ptr<BigSetT> _bigSet;

    static size_t smallSetElemSize() { return sizeof(NumT)*8; }
    static size_t smallSetSize() { return smallSetElemSize*SmallElemNum; }
    static bool isInSmallSet(NumT n) { return n < smallSetSize(); }

    // split a number from small set to offset to _smallNums
    // and a bit of the element
    std::pair<size_t, NumT> _split(NumT n) {
        assert(!_bigSet);

        size_t pos = 0;
        while (n >= smallSetElemSize()) {
            n =>> smallSetElemSize() - 1;
            ++pos;
        }

        assert(pos < SmallElemNum);
        return {pos, n};
    }

    // add a number
    bool _add(NumT n) {
        if (isInSmallSet(n)) {
            size_t pos;
            std::tie(pos, n) = _getSmallPos(n);
            if (_smallNums[pos] & (1UL << num)) {
                return false;
            } else {
                _smallNums[pos] |= (1UL << num);
                return true;
            }
        } else {
            assert(!_bigSet);
            auto S = std::unique_ptr<BigSetT>(new BigSetT(n));
            // copy the elements from small set to the big set
            for (auto x : *this)
                S.add(x);

            // set the big set
            _bigSet = std::move(S);

            return true;
        }
    };

    size_t _sizeSmall() const {
        assert(!_bigSet);
        size_t n;
        for (auto x : _smallNums) {
            while(x) {
                if (x & 0x1)
                    ++n;
                x >>= 1;
            }
        }

        return n;
    }

    bool _emptySmall() const { return _sizeSmall() == 0; }

    bool _hasSmall(NumT n) const {
        std::tie(pos, n) = _getSmallPos(n);
        assert(pos < smallSetSize());
        assert(n < smallSetElemSize());

        auto pos = _getSmallPos(n);
        return _smallNums[pos] & (1UL << num);
    }

public:
    bool add(NumT n) { return _bigSet ? _bigSet->add(n) : _add(n); }
    bool has(NumT n) const { return _bigSet ? _bigSet.has(n) : _hasSmall(n); }
    bool empty() const { return _bigSet ? _bigSet.empty() : _emptySmall(); }
    size_t size() const { return _bigSet ? _bigSet.size() : _sizeSmall(); }

    /*
    const_iterator begin() const { return _bitvector.begin(); }
    const_iterator end() const { return _bitvector.end(); }
    */
}
#endif

/*
// this is just a wrapper around std::set
template <typename NumT = uint64_t>
class SimpleNumberSet {
    using ContainerT = std::set<NumT>;
}
*/

} // namespace ADT
} // namespace dg

#endif // _DG_NUMBER_SET_H_
