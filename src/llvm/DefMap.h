#ifndef _LLVM_DG_DEF_MAP_H_
#define _LLVM_DG_DEF_MAP_H_

#include "AnalysisGeneric.h"
#include <utility>

namespace dg {
namespace analysis {

class DefMap
{
    std::map<Pointer, ValuesSetT> defs;

public:
    typedef std::map<Pointer, ValuesSetT>::iterator iterator;
    typedef std::map<Pointer, ValuesSetT>::const_iterator const_iterator;

    DefMap() {}
    DefMap(const DefMap& o);

    bool merge(const DefMap *o, PointsToSetT *without = nullptr);
    bool add(const Pointer& p, LLVMNode *n);
    bool update(const Pointer& p, LLVMNode *n);
    bool empty() const { return defs.empty(); }

    // @return iterators for the range of pointers that has the same object
    // as the given pointer
    std::pair<DefMap::iterator, DefMap::iterator> getObjectRange(const Pointer& ptr);

    bool defines(const Pointer& p) { return defs.count(p) != 0; }
    bool definesWithAnyOffset(const Pointer& p);

    iterator begin() { return defs.begin(); }
    iterator end() { return defs.end(); }
    const_iterator begin() const { return defs.begin(); }
    const_iterator end() const { return defs.end(); }

    ValuesSetT& get(const Pointer& ptr) { return defs[ptr]; }
    const std::map<Pointer, ValuesSetT> getDefs() const { return defs; }
};

} // analysis
} // dg

#endif
