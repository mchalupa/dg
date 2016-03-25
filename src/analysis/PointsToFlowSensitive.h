#ifndef _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_
#define _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

#include <cassert>

#include "Pointer.h"
#include "PSS.h"

namespace dg {
namespace analysis {
namespace pss {

class PointsToFlowSensitive : public PSS
{
public:
    typedef std::set<MemoryObject *> MemoryObjectsSetT;
    typedef std::map<const Pointer, MemoryObjectsSetT> MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointsToFlowSensitive(PSSNode *r) : PSS(r) {}

    virtual void beforeProcessed(PSSNode *n)
    {
        MemoryMapT *mm = n->getData<MemoryMapT>();
        if (!mm) {
            // on these nodes the memory map can change
            if (n->predecessorsNum() == 0) { // root node
                // FIXME: we're leaking the memory maps
                mm = new MemoryMapT();
            } else if (n->getType() == pss::STORE) {
                mm = new MemoryMapT();

                // create empty memory object so that STORE can
                // store the pointers into it
                for (const Pointer& ptr : n->getOperand(1)->pointsTo) {
                    // FIXME: we're leaking the mem. objects, use autoptr?
                    (*mm)[ptr].insert(new MemoryObject(ptr.target));
                }
            }  else if (n->getType() == pss::MEMCPY) {
                mm = new MemoryMapT();

                // create empty memory object so that MEMCPY can
                // store the pointers into it
                for (const Pointer& ptr : n->getOperand(1)->pointsTo) {
                    // FIXME: we're leaking the mem. objects, use autoptr?
                    (*mm)[ptr].insert(new MemoryObject(ptr.target));
                }
            } else if (n->predecessorsNum() > 1) {
                // this is a join node, create new map and
                // merge the predecessors to it
                mm = new MemoryMapT();

                // merge information from predecessors into new map
                for (PSSNode *p : n->getPredecessors()) {
                    MemoryMapT *pm = p->getData<MemoryMapT>();
                    // merge pm to mm (if pm was already created)
                    if (pm)
                        mergeMaps(mm, pm, nullptr);
                }
            } else {
                PSSNode *pred = n->getSinglePredecessor();
                mm = pred->getData<MemoryMapT>();
                assert(mm && "No memory map in the predecessor");
            }

            // memory map initialized, set it as data,
            // so that we won't initialize it again
            n->setData<MemoryMapT>(mm);
        }
    }

    virtual void afterProcessed(PSSNode *n)
    {
        PointsToSetT *strong_update = nullptr;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        // we must have the memory map, we created it
        // in the beforeProcessed method
        assert(mm && "Do not have memory map");

        // every store is strong update
        // FIXME: memcpy can be strong update too
        if (n->getType() == pss::STORE)
            strong_update = &n->getOperand(1)->pointsTo;

        // merge information from predecessors if there's
        // more of them (if there's just one predecessor
        // and this is not a store, the memory map couldn't
        // change, so we don't have to do that)
        if (n->predecessorsNum() > 1 || strong_update
            || n->getType() == pss::MEMCPY) {
            for (PSSNode *p : n->getPredecessors()) {
                MemoryMapT *pm = p->getData<MemoryMapT>();
                // merge pm to mm (if pm was already created)
                if (pm)
                    mergeMaps(mm, pm, strong_update);
            }
        }
    }

    virtual void getMemoryObjects(PSSNode *where, PSSNode *n,
                                  std::vector<MemoryObject *>& objects)
    {
        //assert(n->getType() == pss::ALLOC || n->getType() == pss::DYN_ALLOC);

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

protected:
    PointsToFlowSensitive() {}

private:
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
};

} // namespace pss
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

