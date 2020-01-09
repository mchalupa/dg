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

    using ParametersContainerTy = std::vector<std::unique_ptr<DGArgumentPair>>;
    ParametersContainerTy _params;

    // wrapper around graphs iterator that unwraps the unique_ptr
    struct params_iterator : public decltype(_params.begin()) {
        using OrigItType = decltype(_params.begin());

        params_iterator() = default;
        params_iterator(const params_iterator& I) = default;
        params_iterator(const OrigItType& I) : OrigItType(I) {}

        DGArgumentPair& operator*() { return *OrigItType::operator*().get(); }
        DGArgumentPair* operator->() { return OrigItType::operator*().get(); }
    };

    class params_range {
        friend class DependenceGraph;

        ParametersContainerTy& _C;

        params_range(ParametersContainerTy& C) : _C(C) {}
    public:
        params_iterator begin() { return params_iterator(_C.begin()); }
        params_iterator end() { return params_iterator(_C.end()); }
    };

public:
    DGParameters(DependenceGraph& dg) : _dg(dg) {}

    DependenceGraph& getDG() { return _dg; }
    const DependenceGraph& getDG() const { return _dg; }

    DGArgumentPair& createParameter() {
        auto *nd = new DGArgumentPair(*this);
        _params.emplace_back(nd);
        return *nd;
    }

    DGArgumentPair& getParameter(unsigned idx) {
        assert(idx < _params.size());
        return *_params[idx].get();
    }

    const DGArgumentPair& getParameter(unsigned idx) const {
        assert(idx < _params.size());
        return *_params[idx].get();
    }

    size_t parametersNum() const { return _params.size(); }

    params_iterator begin() { return params_iterator(_params.begin()); }
    params_iterator end() { return params_iterator(_params.end()); }
};

class DGFormalParameters : public DGParameters {
    friend class DependenceGraph;
    // parameters are associated to this dependence graph
    std::unique_ptr<DGNodeArtificial> _vararg;

    DGFormalParameters(DependenceGraph& dg) : DGParameters(dg) {}

public:

    DGNodeArtificial& createVarArg();
};

class DGNodeCall;

class DGActualParameters : public DGParameters {
    friend class DGNodeCall;
    // these parameters are associated to this call
    DGNodeCall& _call;

    DGActualParameters(DGNodeCall& call);
public:
};

} // namespace sdg
} // namespace dg

#endif // DG_PARAMETERS_H_
