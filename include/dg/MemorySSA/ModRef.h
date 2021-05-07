#ifndef DG_MOD_REF_H_
#define DG_MOD_REF_H_

#include "dg/Offset.h"

#include "dg/MemorySSA/DefinitionsMap.h"
#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

// sumarized information about visible external
// effects of the procedure
class ModRefInfo {
    // to distinguish between empty and non-computed modref information
    bool _initialized{false};

  public:
    // the set of memory that is defined in this procedure
    // and is external to the subgraph or is local but its address is taken
    // In other words, memory whose definitions can be "visible"
    // outside the procedure.
    // FIXME: we should keep only sets of DefSites
    DefinitionsMap<RWNode> maydef;
    // external or local address-taken memory that can be
    // used inside the procedure
    // FIXME: we should keep only sets of DefSites
    DefinitionsMap<RWNode> mayref;
    // memory that must be defined in this procedure
    // (on every path through the procedure)
    DefinitionsMap<RWNode> mustdef;

    void addMayDef(const DefSite &ds, RWNode *def) {
        // FIXME: do not store def, it is useless. Just takes memory...
        maydef.add(ds, def);
    }

    template <typename C>
    void addMayDef(const C &c, RWNode *def) {
        for (auto &ds : c) {
            maydef.add(ds, def);
        }
    }

    void addMayRef(const DefSite &ds, RWNode *ref) {
        // FIXME: do not store ref, it is useless. Just takes memory...
        mayref.add(ds, ref);
    }

    template <typename C>
    void addMayRef(const C &c, RWNode *ref) {
        for (auto &ds : c) {
            mayref.add(ds, ref);
        }
    }

    void addMustDef(const DefSite &ds, RWNode *def) {
        // FIXME: do not store def, it is useless. Just takes memory...
        mustdef.add(ds, def);
    }

    template <typename C>
    void addMustDef(const C &c, RWNode *def) {
        for (auto &ds : c) {
            mustdef.add(ds, def);
        }
    }

    void add(const ModRefInfo &oth) {
        maydef.add(oth.maydef);
        mayref.add(oth.mayref);
        mustdef.add(oth.mustdef);
    }

    ///
    // Check whether the procedure may define 'n' (ignoring writes
    // to unknown memory, \see mayDefineOrUnknown())
    bool mayDefine(RWNode *n) const { return maydef.definesTarget(n); }
    bool mayDefineUnknown() const { return mayDefine(UNKNOWN_MEMORY); }

    ///
    // Check whether the procedure may define 'n', taking into
    // account also writes to unknown memory
    bool mayDefineOrUnknown(RWNode *n) const {
        return mayDefine(n) or mayDefineUnknown();
    }

    auto getMayDef(RWNode *n) -> decltype(maydef.get(n)) {
        return maydef.get(n);
    }

    void setInitialized() { _initialized = true; }
    bool isInitialized() const { return _initialized; }
};

} // namespace dda
} // namespace dg

#endif
