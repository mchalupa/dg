#ifndef DG_DG_NODE_CALL_H_
#define DG_DG_NODE_CALL_H_

#include "DGNode.h"
#include "DGParameters.h"
#include <cassert>
#include <set>

namespace dg {
namespace sdg {

/// ----------------------------------------------------------------------
// Call
/// ----------------------------------------------------------------------
class DGNodeCall : public DGNode {
    // FIXME: change to vector set or smallptr set (small vector set?)
    using CalleesTy = std::set<DependenceGraph *>;

    CalleesTy _callees;
    DGParameters _parameters;

  public:
    DGNodeCall(DependenceGraph &g)
            : DGNode(g, DGElementType::ND_CALL), _parameters(g) {}

    static DGNodeCall *get(DGElement *n) {
        return isa<DGElementType::ND_CALL>(n) ? static_cast<DGNodeCall *>(n)
                                              : nullptr;
    }

    const CalleesTy &getCallees() const { return _callees; }
    bool addCallee(DependenceGraph &g);

    DGParameters &getParameters() { return _parameters; }
    const DGParameters &getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif
