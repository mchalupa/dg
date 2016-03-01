#ifndef _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_
#define _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

#include <cassert>

#include "Pointer.h"
#include "PSS.h"

namespace dg {
namespace analysis {

class PointsToFlowSensitive : public PSS
{
    typedef std::set<MemoryObject *> MemoryObjectsSetT;
    typedef std::map<const Pointer, MemoryObjectsSetT> MemoryMapT;

    void mergeMaps(MemoryMapT *mm, MemoryMapT *pm, PointsToSetT *strong_update)
    {
        for (auto it : *pm) {
            const Pointer& ptr = it.first;
            if (strong_update && strong_update->count(ptr))
                continue;

            MemoryObjectsSetT& S = (*mm)[ptr];
            S.insert(it.second.begin(), it.second.end());
        }
    }

public:
    // this is an easy but not very efficient implementation,
    // works for testing
    PointsToFlowSensitive(PSSNode *r) : PSS(r) {}

    virtual void beforeProcessed(PSSNode *n)
    {
        MemoryMapT *mm = n->getData<MemoryMapT>();
        // FIXME: we don't need to have memory map on every
        // node, just in the root, STORE nodes and join nodes
        if (!mm) {
            mm = new MemoryMapT();
            n->setData<MemoryMapT>(mm);

            if (n->getType() == pss::STORE) {
                for (const Pointer& ptr : n->getOperand(1)->pointsTo)
                    // FIXME: we're leaking it, use autoptr?
                    (*mm)[ptr].insert(new MemoryObject(ptr.target));
            }
        }
    }

    virtual void afterProcessed(PSSNode *n)
    {
        PointsToSetT *strong_update = nullptr;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        assert(mm && "Do not have memory map");

        // every store is strong update
        if (n->getType() == pss::STORE)
            strong_update = &n->getOperand(1)->pointsTo;

        // merge information from predecessors
        for (PSSNode *p : n->getPredecessors()) {
            MemoryMapT *pm = p->getData<MemoryMapT>();
            // merge pm to mm (if pm was already created)
            if (pm)
                mergeMaps(mm, pm, strong_update);
        }
    }

    virtual void getMemoryObjects(PSSNode *where, PSSNode *n,
                                  std::vector<MemoryObject *>& objects)
    {
        assert(n->getType() == pss::ALLOC || n->getType() == pss::DYN_ALLOC);

        MemoryMapT *mm= where->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        // FIXME very unefficient: we could use the ordering on map
        for (auto it : *mm) {
            for (const Pointer& ptr : n->pointsTo) {
                if (it.first.target == ptr.target) {
                    for (MemoryObject *o : it.second)
                        objects.push_back(o);
                }
            }
        }
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

