#ifndef DG_DG_ARG_PAIR_H_
#define DG_DG_ARG_PAIR_H_

#include <cassert>
#include "DGNode.h"

namespace dg {
namespace sdg {

class DGParameters;

class DGArgumentPair : public DGElement {
    friend class DGParameters;

    DGParameters& parameters;

    DGNodeArgument _input;
    DGNodeArgument _output;

    DGArgumentPair(DGParameters& p);
public:
};

} // namespace sdg
} // namespace dg

#endif
