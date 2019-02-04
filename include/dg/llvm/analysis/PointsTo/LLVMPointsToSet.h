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

#include "dg/analysis/PointsTo/PointsToSet.h"

namespace dg {

using analysis::pta::PointsToSetT;
using analysis::pta::PSNode;
using analysis::Offset;

///
// LLVM pointer
//  - value is the allocation site
//  - offset is offset into the memory
struct LLVMPointer {
    llvm::Value *value;
    Offset offset;

    LLVMPointer(llvm::Value *val, Offset o)
    : value(val), offset(o) {}
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
    const PointsToSetT& PTSet;

public:
    class const_iterator {
        const PointsToSetT& PTSet;
        PointsToSetT::const_iterator it;

        const_iterator(const PointsToSetT& S, bool end = false)
        : PTSet(S), it(end ? S.end() : S.begin())  {
            if (!end)
            _find_valid();
        }

        void _find_valid() {
            while (it != PTSet.end() &&
                    (!(*it).isValid() || (*it).isInvalidated()))
                ++it;
        }

    public:
        const_iterator& operator++() {
            ++it;
            _find_valid();
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        LLVMPointer operator*() const {
            auto value = (*it).target->getUserData<llvm::Value>();
            assert(value && "PSNode has associated nullptr as value");
            return LLVMPointer(value, (*it).offset);
        }

        bool operator==(const const_iterator& rhs) const { return it == rhs.it; }
        bool operator!=(const const_iterator& rhs) const { return !operator==(rhs);}

        friend class LLVMPointsToSet;
    };

    LLVMPointsToSet(const PointsToSetT& S) : PTSet(S) {}

    ///
    // NOTE: this may not be O(1) operation
    bool hasUnknown() const { return PTSet.hasUnknown(); }
    bool hasNull() const { return PTSet.hasNull(); }
    bool hasInvalidated() const { return PTSet.hasInvalidated(); }
    bool empty() const { return PTSet.empty(); }
    size_t size() const { return PTSet.size(); }

    bool isSingleton() const { return size() == 1; }
    bool isKnownSingleton() const { return isSingleton()
                                    && !hasUnknown() && !hasNull()
                                    && !hasInvalidated(); }

    LLVMPointer getKnownSingleton() const {
        assert(isKnownSingleton());
        auto ptr = (*(PTSet.begin()));
        return LLVMPointer(ptr.target->getUserData<llvm::Value>(),
                           ptr.offset);
    }

    const_iterator begin() const { return const_iterator(PTSet);}
    const_iterator end() const { return const_iterator(PTSet, true); }
};

} // namespace dg

#endif // _LLVM_DG_POINTS_TO_SET_H_
