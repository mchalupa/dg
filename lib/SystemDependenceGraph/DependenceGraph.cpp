#include <cassert>

#include "dg/SystemDependenceGraph/DependenceGraph.h"
#include "dg/SystemDependenceGraph/DGNode.h"
#include "dg/SystemDependenceGraph/DGNodeCall.h"
#include "dg/SystemDependenceGraph/DGArgumentPair.h"
#include "dg/SystemDependenceGraph/DGParameters.h"

namespace dg {
namespace sdg {

// ------------------------------------------------------------------
// -- Parameters --
// ------------------------------------------------------------------

DGArgumentPair::DGArgumentPair(DGParameters& p)
: DGElement(p.getDG(), DGElementType::ARG_PAIR),
  _parameters(p), _input(p.getDG()), _output(p.getDG()) {}

DGActualParameters::DGActualParameters(DGNodeCall& call)
: DGParameters(call.getDG()), _call(call) {}

DGNodeArtificial& DGFormalParameters::createVarArg() {
    auto& dg = getDG();
    _vararg.reset(&dg.createArtificial());
    return *_vararg.get();
}

DGNode& DGParameters::createNoReturn() {
    auto& dg = getDG();
    _noreturn.reset(&dg.createArtificial());
    return *_noreturn.get();
}


// ------------------------------------------------------------------
// -- Node --
// ------------------------------------------------------------------

unsigned DGNode::getNewID(DependenceGraph& g) {
    return g.getNextNodeID();
}

DGNode::DGNode(DependenceGraph& g, DGElementType t)
: DGElement(g, t), _id(getNewID(g)) {}

bool DGNodeCall::addCallee(DependenceGraph& g) {
    g.addCaller(this);
    return _callees.insert(&g).second;
}

} // namespace sdg
} // namespace dg

