#ifndef DG_DG_NODE_CALL_H_
#define DG_DG_NODE_CALL_H_

#include <cassert>
#include <set>
#include "DGNode.h"
#include "DGParameters.h"

namespace dg {
namespace sdg {

/// ----------------------------------------------------------------------
// Call
/// ----------------------------------------------------------------------
class DGNodeCall : public DGNode {
    std::set<DependenceGraph *> _callees;
    DGParameters _parameters;

public:
    DGNodeCall(DependenceGraph& g)
    : DGNode(g, DGElementType::ND_CALL), _parameters(g) {}

    static DGNodeCall *get(DGElement *n) {
        return isa<DGElementType::ND_CALL>(n) ?
            static_cast<DGNodeCall *>(n) : nullptr;
    }

    const std::set<DependenceGraph *>& getCallees() const { return _callees; }
    bool addCalee(DependenceGraph *g) { return _callees.insert(g).second; }

    DGParameters& getParameters() { return _parameters; }
    const DGParameters& getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif
