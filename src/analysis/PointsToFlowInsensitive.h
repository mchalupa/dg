#ifndef _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_
#define _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_

#include <cassert>

#include "PSS.h"

namespace dg {
namespace analysis {
namespace pss {

class PointsToFlowInsensitive : public PSS
{
public:
    PointsToFlowInsensitive(PSSNode *r) : PSS(r) {}

    virtual void getMemoryObjects(PSSNode *where, PSSNode *n,
                                  std::vector<MemoryObject *>& objects)
    {
        assert(n->getType() == pss::ALLOC || n->getType() == pss::DYN_ALLOC);

        // irrelevant in flow-insensitive
        (void) where;

        MemoryObject *mo = n->getData<MemoryObject>();
        if (!mo) {
            mo = new MemoryObject(n);
            n->setData<MemoryObject>(mo);
        }

        objects.push_back(mo);
    }
};

} // namespace pss
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_

