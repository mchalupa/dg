#ifndef DG_DODNTSCD_H_
#define DG_DODNTSCD_H_

#include <dg/ADT/Bitvector.h>
#include <dg/ADT/Queue.h>
#include <dg/ADT/SetQueue.h>

#include "CDGraph.h"
#include "DOD.h"

namespace dg {

class DODNTSCD : public DOD {
    using ResultT = DOD::ResultT;

    template <typename OnAllPathsT>
    void computeNTSCD(CDNode *p, CDGraph &graph, const OnAllPathsT &onallpaths,
                      ResultT &CD, ResultT &revCD) {
        const auto &succs = p->successors();
        assert(succs.size() == 2);
        auto succit = succs.begin();
        auto *s1 = *succit;
        auto *s2 = *(++succit);
        assert(++succit == succs.end());

        auto it1 = onallpaths.find(s1);
        if (it1 == onallpaths.end())
            return;
        auto it2 = onallpaths.find(s2);
        if (it2 == onallpaths.end())
            return;

        const auto &nodes1 = it1->second;
        const auto &nodes2 = it2->second;
        // FIXME: we could do that faster
        for (auto *n : graph) {
            if (nodes1.get(n->getID()) ^ nodes2.get(n->getID())) {
                CD[n].insert(p);
                revCD[p].insert(n);
            }
        }
    }

  public:
    std::pair<ResultT, ResultT> compute(CDGraph &graph) {
        ResultT CD;
        ResultT revCD;

        DBG_SECTION_BEGIN(cda, "Computing DOD for all predicates");

        AllMaxPath allmaxpath;
        DBG_SECTION_BEGIN(
                cda,
                "Coputing nodes that are on all max paths from nodes for fun "
                        << graph.getName());
        auto allpaths = allmaxpath.compute(graph);
        DBG_SECTION_END(
                cda,
                "Done coputing nodes that are on all max paths from nodes");

        for (auto *p : graph.predicates()) {
            computeDOD(p, graph, allpaths, CD, revCD);
            computeNTSCD(p, graph, allpaths, CD, revCD);
        }

        DBG_SECTION_END(cda, "Finished computing DOD for all predicates");
        return {CD, revCD};
    }
};

}; // namespace dg

#endif
