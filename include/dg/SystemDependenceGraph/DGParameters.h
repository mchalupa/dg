#ifndef DG_SDG_PARAMETERS_H_
#define DG_SDG_PARAMETERS_H_

#include <cassert>
#include <memory>
#include <vector>

#include "DGArgumentPair.h"
#include "DGBBlock.h"

namespace dg {
namespace sdg {

class DependenceGraph;

class DGParameters {
    DependenceGraph &_dg;
    // node representing that the function may not return
    // (it terminates the program or loops forever)
    // NOTE: it is owned by the _dg after creation
    DGNodeArtificial *_noreturn{nullptr};
    // output argument representing the return from the function
    DGNodeArtificial *_return{nullptr};

    using ParametersContainerTy = std::vector<std::unique_ptr<DGArgumentPair>>;
    ParametersContainerTy _params;

    // wrapper around graphs iterator that unwraps the unique_ptr
    struct params_iterator : public decltype(_params.begin()) {
        using OrigItType = decltype(_params.begin());

        params_iterator() = default;
        params_iterator(const params_iterator &I) = default;
        params_iterator(const OrigItType &I) : OrigItType(I) {}

        DGArgumentPair &operator*() { return *OrigItType::operator*(); }
        DGArgumentPair *operator->() { return OrigItType::operator*().get(); }
    };

    class params_range {
        friend class DependenceGraph;

        ParametersContainerTy &_C;

        params_range(ParametersContainerTy &C) : _C(C) {}

      public:
        params_iterator begin() { return params_iterator(_C.begin()); }
        params_iterator end() { return params_iterator(_C.end()); }
    };

  public:
    DGParameters(DependenceGraph &dg) : _dg(dg) {}

    DependenceGraph &getDG() { return _dg; }
    const DependenceGraph &getDG() const { return _dg; }

    DGArgumentPair &createParameter() {
        auto *nd = new DGArgumentPair(*this);
        _params.emplace_back(nd);
        return *nd;
    }

    DGArgumentPair &getParameter(unsigned idx) {
        assert(idx < _params.size());
        return *_params[idx].get();
    }

    const DGArgumentPair &getParameter(unsigned idx) const {
        assert(idx < _params.size());
        return *_params[idx].get();
    }

    size_t parametersNum() const { return _params.size(); }

    params_iterator begin() { return params_iterator(_params.begin()); }
    params_iterator end() { return params_iterator(_params.end()); }

    DGNode &createReturn();
    DGNode *getReturn() { return _return; }
    const DGNode *getReturn() const { return _return; }

    DGNode &createNoReturn();
    DGNode *getNoReturn() { return _noreturn; }
    const DGNode *getNoReturn() const { return _noreturn; }
};

class DGFormalParameters : public DGParameters {
    friend class DependenceGraph;
    // parameters are associated to this dependence graph
    std::unique_ptr<DGNodeArtificial> _vararg;

    DGFormalParameters(DependenceGraph &dg) : DGParameters(dg) {}

  public:
    DGNodeArtificial &createVarArg();
};

class DGNodeCall;

class DGActualParameters : public DGParameters {
    friend class DGNodeCall;
    // these parameters are associated to this call
    DGNodeCall &_call;

    DGActualParameters(DGNodeCall &call);

  public:
    DGNodeCall &getCall() { return _call; }
    const DGNodeCall &getCall() const { return _call; }
};

} // namespace sdg
} // namespace dg

#endif // DG_SDG_PARAMETERS_H_
