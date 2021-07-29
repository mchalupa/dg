#ifndef LLVM_DG_POINTS_TO_SET_H_
#define LLVM_DG_POINTS_TO_SET_H_

#include <cassert>
#include <utility>

#include <llvm/IR/Value.h>

#include "dg/PointerAnalysis/PointsToSet.h"

namespace dg {

using pta::PointsToSetT;
using pta::PSNode;

///
// LLVM pointer
//  - value is the allocation site
//  - offset is offset into the memory
struct LLVMPointer {
    llvm::Value *value;
    Offset offset;

    LLVMPointer(llvm::Value *val, Offset o) : value(val), offset(o) {
        assert(val && "nullptr passed as value");
    }

    bool operator==(const LLVMPointer &rhs) const {
        return value == rhs.value && offset == rhs.offset;
    }

    ///
    // Memory locations described by this pointer cover
    // (are a supperset) the memory locations of the rhs pointer.
    bool covers(const LLVMPointer &rhs) const {
        return value == rhs.value &&
               (offset.isUnknown() || offset == rhs.offset);
    }
};

///
// LLVM memory region
// Pointer + length of referenced memory
struct LLVMMemoryRegion {
    LLVMPointer pointer;
    Offset len;

    LLVMMemoryRegion(LLVMPointer ptr, const Offset l)
            : pointer(std::move(ptr)), len(l) {}

    LLVMMemoryRegion(llvm::Value *val, const Offset off, const Offset l)
            : pointer(val, off), len(l) {}
};

///
// A set of memory regions
// XXX: we should create more efficient implementation.
class LLVMMemoryRegionSet {
    struct OffsetPair {
        const Offset offset{0};
        const Offset len{0};

        OffsetPair() = default;
        OffsetPair(const OffsetPair &rhs) = default;
        OffsetPair(const Offset o, const Offset l) : offset(o), len(l) {}

        bool overlaps(const OffsetPair interval) const {
            return overlaps(interval.offset, interval.len);
        }

        bool overlaps(const Offset o, const Offset l) const {
            if (o.isUnknown() || offset.isUnknown())
                return true;

            if (o < offset) {
                if (l.isUnknown() || o + l >= offset) {
                    return true;
                }
            } else {
                return o <= offset + len;
            }

            return false;
        }

        bool coveredBy(const OffsetPair &rhs) const {
            return coveredBy(rhs.offset, rhs.len);
        }

        bool coveredBy(const Offset o, const Offset l) const {
            assert(!o.isUnknown() && !offset.isUnknown());
            // we allow len == UNKNOWN and treat it as infinity

            return (o <= offset && ((l.isUnknown() && len.isUnknown()) ||
                                    l.isUnknown() || l >= len));
        }

        bool extends(const OffsetPair &rhs) const {
            return rhs.coveredBy(*this);
        }

        bool extends(const Offset o, const Offset l) const {
            return extends({o, l});
        }
    };

    using MappingT = std::map<llvm::Value *, std::vector<OffsetPair>>;

    // intervals of bytes for each memory
    // (llvm::Value corresponding to the allocation)
    MappingT _regions;

    static std::pair<Offset, Offset>
    _extend(const OffsetPair interval, const Offset off, const Offset len) {
        assert(interval.overlaps(off, len));
        assert(!off.isUnknown());

        Offset o = interval.offset;
        Offset l = interval.len;

        if (off < interval.len)
            o = off;
        if (interval.len < len || len.isUnknown()) {
            l = len;
        }

        return std::make_pair(o, l);
    }

    const std::vector<OffsetPair> *_get(llvm::Value *v) const {
        auto it = _regions.find(v);
        return it == _regions.end() ? nullptr : &it->second;
    }

  public:
    // Add a memory region to this set
    //
    // not very efficient, but we will use it only
    // for transfering the results, so it should be fine
    void add(llvm::Value *mem, const Offset o, const Offset l) {
        auto &R = _regions[mem];
        // we do not know the bytes in this region
        if (o.isUnknown()) {
            R.clear();
            R.emplace_back(o, o);
            return;
        }

        assert(!o.isUnknown());

        for (auto &interval : R) {
            if (interval.offset.isUnknown() || interval.extends(o, l)) {
                return; // nothing to be done
            }
        }

        // join all overlapping intervals
        Offset newO = o;
        Offset newL = l;
        for (auto &interval : R) {
            if (interval.overlaps(newO, newL)) {
                std::tie(newO, newL) = _extend(interval, newO, newL);
            }
        }

        std::vector<OffsetPair> tmp;
        tmp.reserve(R.size());

        for (auto &interval : R) {
            // get rid of covered intervals
            if (interval.coveredBy(newO, newL))
                continue;

            // if intervals overlap, join them,
            // otherwise keep the original interval
            assert(!interval.overlaps(newO, newL));
            tmp.push_back(interval);
        }

        tmp.emplace_back(newO, newL);
        tmp.swap(R);

#ifndef NDEBUG
        for (auto &interval : R) {
            assert(((interval.offset == newO && interval.len == newL) ||
                    !interval.overlaps(newO, newL)) &&
                   "Joined intervals incorrectly");
        }
#endif // NDEBUG
    }

    // XXX: inefficient
    bool overlaps(const LLVMMemoryRegionSet &rhs) const {
        for (const auto &it : rhs._regions) {
            const auto *our = _get(it.first);
            if (!our) {
                continue;
            }

            for (const auto &interval : *our) {
                for (const auto &interval2 : it.second) {
                    if (interval.overlaps(interval2))
                        return true;
                }
            }
        }

        return false;
    }

    class const_iterator {
        size_t pos{0};
        typename MappingT::const_iterator it;

        const_iterator(const MappingT::const_iterator I) : it(I) {}

      public:
        LLVMMemoryRegion operator*() const {
            return LLVMMemoryRegion{it->first, it->second[pos].offset,
                                    it->second[pos].len};
        }

        const_iterator &operator++() {
            ++pos;
            if (pos >= it->second.size()) {
                ++it;
                pos = 0;
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        bool operator==(const const_iterator &rhs) const {
            return it == rhs.it && pos == rhs.pos;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class LLVMMemoryRegionSet;
    };

    const_iterator begin() const { return {_regions.begin()}; }
    const_iterator end() const { return {_regions.end()}; }
};

///
// Implementation of LLVMPointsToSet
class LLVMPointsToSetImpl {
  public:
    ///
    // NOTE: this may not be O(1) operation
    virtual bool hasUnknown() const = 0;
    virtual bool hasNull() const = 0;
    virtual bool hasNullWithOffset() const = 0;
    virtual bool hasInvalidated() const = 0;
    virtual size_t size() const = 0;

    virtual LLVMPointer getKnownSingleton() const = 0;

    // for iterator implementation
    // XXX: merge position() and end()?
    virtual int position() const = 0;    // for comparision
    virtual bool end() const = 0;        // have done iteration?
    virtual void shift() = 0;            // iterate
    virtual LLVMPointer get() const = 0; // dereference

    virtual ~LLVMPointsToSetImpl() = default;
};

///
// Wrapper for PointsToSet with iterators that yield LLVMPointer,
// so that mapping pointer analysis results to LLVM is opaque for
// the user. The special nodes like unknown memory and null
// are not yield by the iterators. Instead, the class has methods
// hasUnknown() and hasNull() to express these properties.
// This also means that it is possible that iterating over the
// set yields no elements, but empty() == false
// (the set contains only unknown or null elements)
// XXX: would it be easier and actually more efficient
// just to return a std::vector<LLVMPointer> instead
// of all this wrapping?
class LLVMPointsToSet {
    std::unique_ptr<LLVMPointsToSetImpl> _impl{};

  public:
    class const_iterator {
        LLVMPointsToSetImpl *impl{nullptr};

        // impl = null means end iterator
        void _check_end() {
            assert(impl);
            if (impl->end())
                impl = nullptr;
        }

        const_iterator(LLVMPointsToSetImpl *impl = nullptr) : impl(impl) {
            // the iterator (impl) may have been shifted during initialization
            // to the end() (because it contains no llvm::Values or the
            // ptset is empty)
            if (impl)
                _check_end();
        }

      public:
        const_iterator &operator++() {
            impl->shift();
            _check_end();
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        LLVMPointer operator*() const {
            assert(impl && "Has no impl");
            return impl->get();
        }

        bool operator==(const const_iterator &rhs) const {
            if (!impl)
                return !rhs.impl;
            if (!rhs.impl)
                return !impl;

            assert(!impl->end() && rhs.impl->end());
            assert(impl == rhs.impl &&
                   "Compared unrelated iterators"); // catch bugs
            llvm::errs() << "CMP" << impl << "+" << impl->position()
                         << " == " << rhs.impl << "+" << rhs.impl->position()
                         << "\n";
            return impl == rhs.impl && impl->position() == rhs.impl->position();
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class LLVMPointsToSet;
    };

    LLVMPointsToSet(LLVMPointsToSetImpl *impl) : _impl(impl) {}
    LLVMPointsToSet() = default;
    LLVMPointsToSet(LLVMPointsToSet &&) = default;
    LLVMPointsToSet &operator=(LLVMPointsToSet &&) = default;

    ///
    // NOTE: this may not be O(1) operation
    bool hasUnknown() const { return _impl->hasUnknown(); }
    bool hasNull() const { return _impl->hasNull(); }
    bool hasNullWithOffset() const { return _impl->hasNullWithOffset(); }
    bool hasInvalidated() const { return _impl->hasInvalidated(); }
    bool empty() const { return _impl->size() == 0; }
    size_t size() const { return _impl->size(); }

    bool isSingleton() const { return _impl->size() == 1; }
    bool isKnownSingleton() const {
        return isSingleton() && !_impl->hasUnknown() && !_impl->hasNull() &&
               !_impl->hasInvalidated();
    }

    // matches {unknown}
    bool isUnknownSingleton() const { return isSingleton() && hasUnknown(); }

    LLVMPointer getKnownSingleton() const { return _impl->getKnownSingleton(); }

    const_iterator begin() const { return {_impl.get()}; }
    static const_iterator end() { return {nullptr}; }
};

/// Auxiliary template that may be used when implementing LLVMPointsToSetImpl
template <typename PTSetT>
class LLVMPointsToSetImplTemplate : public LLVMPointsToSetImpl {
  protected:
    PTSetT PTSet;
    decltype(PTSet.begin()) it;
    size_t _position{0};

    bool issingleton() const {
        // is there any better way how to do it?
        auto it = PTSet.begin();
        return it != PTSet.end() && ++it == PTSet.end();
    }

    bool isKnownSingleton() const {
        return issingleton() && !hasUnknown() && !hasNull() &&
               !hasInvalidated();
    }

    // find next node that has associated llvm::Value
    // (i.e. skip null, unknown, etc.)
    virtual void _findNextReal() = 0;

    void initialize_iterator() {
        assert(it == PTSet.begin());
        if (!PTSet.empty())
            _findNextReal();
    }

  public:
    // NOTE: the child constructor must call initialize_iterator().
    // We cannot call it here since you can't call virtual
    // functions in ctor/dtor
    LLVMPointsToSetImplTemplate(PTSetT S) : PTSet(S), it(PTSet.begin()) {}

    void shift() override {
        assert(it != PTSet.end() && "Tried to shift end() iterator");
        ++it;
        ++_position;
        _findNextReal();
    }

    int position() const override { return _position; }
    bool end() const override { return it == PTSet.end(); }

    // NOTE: LLVMPointsToSet will overtake the ownership of this
    // object and will delete it on destruction.
    LLVMPointsToSet toLLVMPointsToSet() { return LLVMPointsToSet(this); }
};

/// Implementation of LLVMPointsToSet that iterates
//  over the DG's points-to set
class DGLLVMPointsToSet
        : public LLVMPointsToSetImplTemplate<const PointsToSetT &> {
    void _findNextReal() override {
        while (it != PTSet.end() &&
               (!(*it).isValid() || (*it).isInvalidated())) {
            ++it;
            ++_position;
        }
    }

  public:
    DGLLVMPointsToSet(const PointsToSetT &S) : LLVMPointsToSetImplTemplate(S) {
        initialize_iterator();
    }

    ///
    // NOTE: this may not be O(1) operation
    bool hasUnknown() const override { return PTSet.hasUnknown(); }
    bool hasNull() const override { return PTSet.hasNull(); }
    bool hasNullWithOffset() const override {
        return PTSet.hasNullWithOffset();
    }
    bool hasInvalidated() const override { return PTSet.hasInvalidated(); }
    size_t size() const override { return PTSet.size(); }

    LLVMPointer getKnownSingleton() const override {
        assert(isKnownSingleton());
        auto ptr = (*(PTSet.begin()));
        return {ptr.target->getUserData<llvm::Value>(), ptr.offset};
    }

    LLVMPointer get() const override {
        assert((it != PTSet.end()) && "Dereferenced end() iterator");
        return LLVMPointer{(*it).target->getUserData<llvm::Value>(),
                           (*it).offset};
    }
};

} // namespace dg

#endif // LLVM_DG_POINTS_TO_SET_H_
