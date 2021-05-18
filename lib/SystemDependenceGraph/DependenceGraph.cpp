#include <cassert>

#include "dg/SystemDependenceGraph/DGArgumentPair.h"
#include "dg/SystemDependenceGraph/DGNode.h"
#include "dg/SystemDependenceGraph/DGNodeCall.h"
#include "dg/SystemDependenceGraph/DGParameters.h"
#include "dg/SystemDependenceGraph/DependenceGraph.h"

namespace dg {
namespace sdg {

// ------------------------------------------------------------------
// -- Parameters --
// ------------------------------------------------------------------

DGArgumentPair::DGArgumentPair(DGParameters &p)
        : DGElement(p.getDG(), DGElementType::ARG_PAIR), _parameters(p),
          _input(p.getDG()), _output(p.getDG()) {}

DGActualParameters::DGActualParameters(DGNodeCall &call)
        : DGParameters(call.getDG()), _call(call) {}

DGNodeArtificial &DGFormalParameters::createVarArg() {
    auto &dg = getDG();
    _vararg.reset(&dg.createArtificial());
    return *_vararg;
}

DGNode &DGParameters::createNoReturn() {
    auto &dg = getDG();
    _noreturn = &dg.createArtificial();
    return *_noreturn;
}

DGNode &DGParameters::createReturn() {
    auto &dg = getDG();
    _return = &dg.createArtificial();
    return *_return;
}

// ------------------------------------------------------------------
// -- DGElem --
// ------------------------------------------------------------------

unsigned DGElement::getNewID(DependenceGraph &g) { return g.getNextNodeID(); }

DGElement::DGElement(DependenceGraph &g, DGElementType t)
        : _id(getNewID(g)), _type(t), _dg(g) {}

// ------------------------------------------------------------------
// -- Node --
// ------------------------------------------------------------------

DGNode::DGNode(DependenceGraph &g, DGElementType t) : DepDGElement(g, t) {
    assert(t > DGElementType::NODE && "Invalid node type");
}

bool DGNodeCall::addCallee(DependenceGraph &g) {
    g.addCaller(this);
    return _callees.insert(&g).second;
}

} // namespace sdg
} // namespace dg
