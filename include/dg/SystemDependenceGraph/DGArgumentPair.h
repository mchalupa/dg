#ifndef DG_DG_ARG_PAIR_H_
#define DG_DG_ARG_PAIR_H_

#include "DGNode.h"
#include <cassert>

namespace dg {
namespace sdg {

class DGParameters;

///
// Input-output pair of arguments that is associated to some (formal or
// actual) DGParameters object.
class DGArgumentPair : public DGElement {
    friend class DGParameters;

    DGParameters &_parameters;

    DGNodeArgument _input;
    DGNodeArgument _output;

    DGArgumentPair(DGParameters &p);

  public:
    static DGArgumentPair *get(DGElement *n) {
        return isa<DGElementType::ARG_PAIR>(n)
                       ? static_cast<DGArgumentPair *>(n)
                       : nullptr;
    }

    DGNodeArgument &getInputArgument() { return _input; }
    const DGNodeArgument &getInputArgument() const { return _input; }

    DGNodeArgument &getOutputArgument() { return _output; }
    const DGNodeArgument &getOutputArgument() const { return _output; }

    DGParameters &getParameters() { return _parameters; }
    const DGParameters &getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif
