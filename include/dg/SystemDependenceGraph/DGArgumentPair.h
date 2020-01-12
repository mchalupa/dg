#ifndef DG_DG_ARG_PAIR_H_
#define DG_DG_ARG_PAIR_H_

#include <cassert>
#include "DGNode.h"

namespace dg {
namespace sdg {

class DGParameters;

class DGArgumentPair : public DGElement {
    friend class DGParameters;

    DGParameters& _parameters;

    DGNodeArgument _input;
    DGNodeArgument _output;

    DGArgumentPair(DGParameters& p);
public:

    static DGArgumentPair* get(DGElement *n) {
        return isa<DGElementType::ARG_PAIR>(n) ?
            static_cast<DGArgumentPair *>(n) : nullptr;
    }

    DGNodeArgument& getInputArgument() { return _input; }
    const DGNodeArgument& getInputArgument() const { return _input; }

    DGNodeArgument& getOutputArgument() { return _output; }
    const DGNodeArgument& getOutputArgument() const { return _output; }

    DGParameters& getParameters() { return _parameters; }
    const DGParameters& getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif
