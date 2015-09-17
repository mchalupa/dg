#ifndef _LLVM_DG_DEF_MAP_H_
#define _LLVM_DG_DEF_MAP_H_

#include "AnalysisGeneric.h"

namespace dg {
namespace analysis {

class DefMap
{
    // last definition of memory location
    // pointed to by the Pointer
    typedef std::map<Pointer, ValuesSetT>::iterator iterator;
    typedef std::map<Pointer, ValuesSetT>::const_iterator const_iterator;
    std::map<Pointer, ValuesSetT> defs;

public:
    DefMap() {}
    DefMap(const DefMap& o);

    bool merge(const DefMap *o, PointsToSetT *without = nullptr);
    bool add(const Pointer& p, LLVMNode *n);
    bool update(const Pointer& p, LLVMNode *n);

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
