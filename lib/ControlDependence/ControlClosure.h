#ifndef CD_CONTROL_CLOSURE_H_
#define CD_CONTROL_CLOSURE_H_

#include <vector>
#include <set>

#include "CDGraph.h"

namespace dg {

class StrongControlClosure {
public:
    using ValVecT = std::vector<CDNode *>;

    ValVecT closeSet(const std::set<CDNode *>& nodes) {
        abort();
        return {};
    }
};

} // namespace dg

#endif
