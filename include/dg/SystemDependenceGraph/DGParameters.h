#ifndef DG_PARAMETERS_H_
#define DG_PARAMETERS_H_

#include <cassert>
#include <vector>
#include <memory>

#include "DGArgumentPair.h"
#include "DGBBlock.h"

namespace dg {
namespace sdg {

class DependenceGraph;

class DGParameters {
    DependenceGraph& _dg;
    std::unique_ptr<DGNodeArtificial> _noreturn;

    std::vector<std::unique_ptr<DGArgumentPair>> _params;

public:
    DGParameters(DependenceGraph& dg) : _dg(dg) {}

    DependenceGraph& getDG() { return _dg; }
    const DependenceGraph& getDG() const { return _dg; }

    DGArgumentPair& createParameter() {
        auto *nd = new DGArgumentPair(*this); 
        _params.emplace_back(nd);
        return *nd;
    }
};

class DGFormalParameters : public DGParameters {
    friend class DependenceGraph;
    // parameters are associated to this dependence graph
    std::unique_ptr<DGNodeArtificial> _vararg;

    DGFormalParameters(DependenceGraph& dg) : DGParameters(dg) {}

public:

    DGNodeArtificial& createVarArg();
};

class DGActualParameters : public DGParameters {
    friend class DGNodeCall;
    // these parameters are associated to this call
    DGNodeCall& _call;

    DGActualParameters(DGNodeCall& call) : DGParameters(call.getDG()), _call(call) {}
public:
};

} // namespace sdg
} // namespace dg

#endif // DG_PARAMETERS_H_
