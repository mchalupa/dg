#ifndef _LLVM_DG_POINTS_TO_SET_H_
#define _LLVM_DG_POINTS_TO_SET_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Value.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

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

    LLVMPointer(llvm::Value *val, Offset o)
    : value(val), offset(o) {
        assert(val && "nullptr passed as value");
    }
};

///
// Implementation of LLVMPointsToSet
class LLVMPointsToSetImpl {
public:
    ///
    // NOTE: this may not be O(1) operation
    virtual bool hasUnknown() const = 0;
    virtual bool hasNull() const = 0;
    virtual bool hasInvalidated() const = 0;
    virtual size_t size() const = 0;

    virtual LLVMPointer getKnownSingleton() const  = 0;

    // for iterator implementation
    // XXX: merge position() and end()?
    virtual int position() const = 0; // for comparision
    virtual bool end() const = 0; // have done iteration?
    virtual void shift() = 0;  // iterate
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
        const_iterator& operator++() {
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
            return impl->get();
        }

        bool operator==(const const_iterator& rhs) const {
            if (!impl)
                return !rhs.impl;
            if (!rhs.impl)
                return !impl;

            assert(!impl->end() && rhs.impl->end());
            assert(impl == rhs.impl && "Compared unrelated iterators"); // catch bugs
            llvm::errs() << "CMP" << impl << "+" << impl->position() << " == "
                                  << rhs.impl << "+" << rhs.impl->position() << "\n";
            return impl == rhs.impl && impl->position() == rhs.impl->position();
        }

        bool operator!=(const const_iterator& rhs) const { return !operator==(rhs);}

        friend class LLVMPointsToSet;
    };

    LLVMPointsToSet(LLVMPointsToSetImpl *impl) : _impl(impl) {}

    ///
    // NOTE: this may not be O(1) operation
    bool hasUnknown() const { return _impl->hasUnknown(); }
    bool hasNull() const { return _impl->hasNull(); }
    bool hasInvalidated() const { return _impl->hasInvalidated(); }
    bool empty() const { return _impl->size() == 0; }
    size_t size() const { return _impl->size(); }

    bool isSingleton() const { return _impl->size() == 1; }
    bool isKnownSingleton() const { return isSingleton()
                                    && !_impl->hasUnknown()
                                    && !_impl->hasNull()
                                    && !_impl->hasInvalidated(); }

    LLVMPointer getKnownSingleton() const { return _impl->getKnownSingleton(); }

    const_iterator begin() const { return const_iterator(_impl.get());}
    const_iterator end() const { return const_iterator(nullptr); }
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

    bool isKnownSingleton() const { return issingleton()
                                    && !hasUnknown() && !hasNull()
                                    && !hasInvalidated(); }

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
    LLVMPointsToSetImplTemplate(PTSetT S)
    : PTSet(S), it(PTSet.begin()) {
    }

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
    LLVMPointsToSet toLLVMPointsToSet() {
        return LLVMPointsToSet(this);
    }
};


/// Implementation of LLVMPointsToSet that iterates
//  over the DG's points-to set
class DGLLVMPointsToSet : public LLVMPointsToSetImplTemplate<const PointsToSetT&> {

    void _findNextReal() override {
        while (it != PTSet.end() &&
               (!(*it).isValid() || (*it).isInvalidated())) {
            ++it;
            ++_position;
        }
    }

public:
    DGLLVMPointsToSet(const PointsToSetT& S) : LLVMPointsToSetImplTemplate(S) {
        initialize_iterator();
    }

    ///
    // NOTE: this may not be O(1) operation
    bool hasUnknown() const override { return PTSet.hasUnknown(); }
    bool hasNull() const override { return PTSet.hasNull(); }
    bool hasInvalidated() const override { return PTSet.hasInvalidated(); }
    size_t size() const override { return PTSet.size(); }

    LLVMPointer getKnownSingleton() const override {
        assert(isKnownSingleton());
        auto ptr = (*(PTSet.begin()));
        return LLVMPointer(ptr.target->getUserData<llvm::Value>(),
                           ptr.offset);
    }

    LLVMPointer get() const override {
        assert((it != PTSet.end()) && "Dereferenced end() iterator");
        return LLVMPointer{(*it).target->getUserData<llvm::Value>(), (*it).offset};
    }
};

} // namespace dg

#endif // _LLVM_DG_POINTS_TO_SET_H_
