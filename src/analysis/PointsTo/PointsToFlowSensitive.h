#ifndef _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_
#define _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

#include <cassert>

#include "Pointer.h"
#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

class PointsToFlowSensitive : public PointerAnalysis
{
public:
    typedef std::set<MemoryObject *> MemoryObjectsSetT;
    typedef std::map<const Pointer, MemoryObjectsSetT> MemoryMapT;

    // this is an easy but not very efficient implementation,
    // works for testing
    PointsToFlowSensitive(PointerSubgraph *ps)
    : PointerAnalysis(ps, UNKNOWN_OFFSET, false) {}

    static bool canChangeMM(PSNode *n) {
        if (n->predecessorsNum() == 0 || // root node
            n->getType() == PSNodeType::STORE ||
            n->getType() == PSNodeType::MEMCPY)
            return true;

        return false;
    }

    bool beforeProcessed(PSNode *n) override
    {
        MemoryMapT *mm = n->getData<MemoryMapT>();
        if (mm)
            return false;

        bool changed = false;

        // on these nodes the memory map can change
        if (canChangeMM(n)) { // root node
            // FIXME: we're leaking the memory maps
            mm = new MemoryMapT();
        } else if (n->predecessorsNum() > 1) {
            // this is a join node, create a new map and
            // merge the predecessors to it
            mm = new MemoryMapT();

            // merge information from predecessors into the new map
            // XXX: this is necessary also with the merge in afterProcess,
            // because this copies the information even for single
            // predecessor, whereas afterProcessed copies the
            // information only for two or more predecessors
            for (PSNode *p : n->getPredecessors()) {
                MemoryMapT *pm = p->getData<MemoryMapT>();
                // merge pm to mm (if pm was already created)
                if (pm) {
                    changed |= mergeMaps(mm, pm, nullptr);
                }
            }
        } else {
            // this node can not change the memory map,
            // so just add a pointer from the predecessor
            // to this map
            PSNode *pred = n->getSinglePredecessor();
            mm = pred->getData<MemoryMapT>();
            assert(mm && "No memory map in the predecessor");
        }

        assert(mm && "Did not create the MM");

        // memory map initialized, set it as data,
        // so that we won't initialize it again
        n->setData<MemoryMapT>(mm);

        // ignore any changes here except when we merged some new information.
        // The other changes we'll detect later
        return changed;
    }

    bool afterProcessed(PSNode *n) override
    {
        bool changed = false;
        PointsToSetT *strong_update = nullptr;

        MemoryMapT *mm = n->getData<MemoryMapT>();
        // we must have the memory map, we created it
        // in the beforeProcessed method
        assert(mm && "Do not have memory map");

        // every store is a strong update
        // FIXME: memcpy can be strong update too
        if (n->getType() == PSNodeType::STORE)
            strong_update = &n->getOperand(1)->pointsTo;

        // merge information from predecessors if there's
        // more of them (if there's just one predecessor
        // and this is not a store, the memory map couldn't
        // change, so we don't have to do that)
        if (n->predecessorsNum() > 1 || strong_update
            || n->getType() == PSNodeType::MEMCPY) {
            for (PSNode *p : n->getPredecessors()) {
                MemoryMapT *pm = p->getData<MemoryMapT>();
                // merge pm to mm (but only if pm was already created)
                if (pm) {
                    changed |= mergeMaps(mm, pm, strong_update);
                }
            }
        }

        return changed;
    }

    void getMemoryObjects(PSNode *where, const Pointer& pointer,
                          std::vector<MemoryObject *>& objects) override
    {
        MemoryMapT *mm= where->getData<MemoryMapT>();
        assert(mm && "Node does not have memory map");

        auto bounds = getObjectRange(mm, pointer);
        for (MemoryMapT::iterator I = bounds.first; I != bounds.second; ++I) {
            assert(I->first.target == pointer.target
                    && "Bug in getObjectRange");

            for (MemoryObject *mo : I->second)
                objects.push_back(mo);
        }

        assert(bounds.second->first.target != pointer.target
                && "Bug in getObjectRange");

        // if we haven't found any memory object, but this psnode
        // is a write to memory, create a new one, so that
        // the write has something to write to
        if (objects.empty() && canChangeMM(where)) {
            MemoryObject *mo = new MemoryObject(pointer.target);
            (*mm)[pointer].insert(mo);
            objects.push_back(mo);
        }
    }

protected:

    PointsToFlowSensitive() = default;

private:

    static bool comp(const std::pair<const Pointer, MemoryObjectsSetT>& a,
                     const std::pair<const Pointer, MemoryObjectsSetT>& b) {
        return a.first.target < b.first.target;
    }

    ///
    // get interator range for elements that have information
    // about the ptr.target node (ignoring the offsets)
    std::pair<MemoryMapT::iterator, MemoryMapT::iterator>
    getObjectRange(MemoryMapT *mm, const Pointer& ptr) {
        std::pair<const Pointer, MemoryObjectsSetT> what(ptr, MemoryObjectsSetT());
        return std::equal_range(mm->begin(), mm->end(), what, comp);
    }

    ///
    // Merge two Memory maps, return true if any new information was created,
    // otherwise return false
    bool mergeMaps(MemoryMapT *mm, MemoryMapT *pm,
                   PointsToSetT *strong_update) {
        bool changed = false;
        for (auto& it : *pm) {
            const Pointer& ptr = it.first;
            if (strong_update && strong_update->count(ptr))
                continue;

            // use [] to create the object if needed
            MemoryObjectsSetT& S = (*mm)[ptr];
            for (auto& elem : it.second)
                changed |= S.insert(elem).second;
        }

        return changed;
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_ANALYSIS_POINTS_TO_FLOW_SENSITIVE_H_

