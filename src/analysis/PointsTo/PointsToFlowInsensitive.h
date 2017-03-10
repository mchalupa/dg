#ifndef _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_
#define _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_

#include <cassert>
#include <vector>

#include "PointerAnalysis.h"

namespace dg {
namespace analysis {
namespace pta {

class PointsToFlowInsensitive : public PointerAnalysis
{
    PointerSubgraph *ps;

protected:
    PointsToFlowInsensitive() = default;

public:
    PointsToFlowInsensitive(PointerSubgraph *ps)
    : PointerAnalysis(ps), ps(ps) {}

    ~PointsToFlowInsensitive() {
        std::vector<PSNode *> nodes = ps->getNodes();
        for (PSNode *n : nodes) {
            MemoryObject *mo = n->getData<MemoryObject>();
            delete mo;
        }
    }

    virtual void getMemoryObjects(PSNode *where, const Pointer& pointer,
                                  std::vector<MemoryObject *>& objects)
    {
        // irrelevant in flow-insensitive
        (void) where;
        PSNode *n = pointer.target;

        // we want to have memory in allocation sites
        if (n->getType() == PSNodeType::CAST || n->getType() == PSNodeType::GEP)
            n = n->getOperand(0);
        else if (n->getType() == PSNodeType::CONSTANT) {
            assert(n->pointsTo.size() == 1);
            n = (n->pointsTo.begin())->target;
        }

        if (n->getType() == PSNodeType::FUNCTION)
            return;

        assert(n->getType() == PSNodeType::ALLOC
               || n->getType() == PSNodeType::DYN_ALLOC
               || n->getType() == PSNodeType::UNKNOWN_MEM);

        MemoryObject *mo = n->getData<MemoryObject>();
        if (!mo) {
            mo = new MemoryObject(n);
            n->setData<MemoryObject>(mo);
        }

        objects.push_back(mo);
    }

    virtual void afterProcessed(PSNode *n)
    {
        (void) n;
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_FLOW_INSENSITIVE_H_

