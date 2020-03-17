#include <set>
#include <vector>
#include <functional>

#ifndef NDEBUG
#include <iostream>
#endif

#include "dg/ReachingDefinitions/RDMap.h"
#include "dg/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/BBlocksBuilder.h"
#include "dg/BFS.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

RWNode UNKNOWN_MEMLOC;
RWNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

#ifndef NDEBUG
void RWNode::dump() const {
	std::cout << getID() << "\n";
}
#endif

const std::vector<RWNode *>& getPredecessors(RWNode *node) {
    return node->getPredecessors();
}

bool ReachingDefinitionsAnalysis::processNode(RWNode *node)
{
    bool changed = false;

    // merge maps from predecessors
    for (RWNode *n : getPredecessors(node))
        changed |= node->def_map.merge(
                    &n->def_map,
                    &node->overwrites /* strong update */,
                    options.strongUpdateUnknown,
                    *options.maxSetSize, /* max size of set of reaching definition
                                            of one definition site */
                    false /* merge unknown */);

    return changed;
}

template <typename ContainerOrNode>
std::vector<RWNode *> getNodes(const ContainerOrNode& start,
                               bool interprocedural = true,
                               unsigned expected_num = 0) {
   static unsigned dfsnum;

   ++dfsnum;

   std::vector<RWNode *> cont;
   if (expected_num != 0)
       cont.reserve(expected_num);

   struct DfsIdTracker {
       const unsigned dfsnum;
       DfsIdTracker(unsigned dnum) : dfsnum(dnum) {}

       void visit(RWNode *n) { n->dfsid = dfsnum; }
       bool visited(RWNode *n) const { return n->dfsid == dfsnum; }
   };

    // iterate over successors and call (return) edges
   struct EdgeChooser {
       const bool interproc;
       EdgeChooser(bool inter = true) : interproc(inter) {}

       void foreach(RWNode *cur, std::function<void(RWNode *)> Dispatch) {
           if (interproc) {
               if (RWNodeCall *C = RWNodeCall::get(cur)) {
                   for (auto& calledVal : C->getCallees()) {
                       if (auto *subg = calledVal.getSubgraph()) {
                           Dispatch(const_cast<RWNode *>(subg->getRoot()));
                       }
                   }
                   // we do not need to iterate over succesors
                   // if we dive into the procedure (as we will
                   // return via call return)
                   // NOTE: we must iterate over successors if the
                   // function is undefined
                   if (!C->getCallees().empty())
                       return;
               } else if (RWNodeRet *R = RWNodeRet::get(cur)) {
                   for (auto ret : R->getReturnSites()) {
                       Dispatch(ret);
                   }
                   if (!R->getReturnSites().empty())
                       return;
               }
           }

           for (auto s : cur->getSuccessors())
               Dispatch(s);
       }
   };

   DfsIdTracker visitTracker(dfsnum);
   EdgeChooser chooser(interprocedural);
   BFS<RWNode, DfsIdTracker, EdgeChooser> bfs(visitTracker, chooser);

   bfs.run(start, [&cont](RWNode *n) { cont.push_back(n); });

   return cont;
}

void ReachingDefinitionsAnalysis::run()
{
    DBG_SECTION_BEGIN(dda, "Starting reaching definitions analysis");
    assert(getRoot() && "Do not have root");

    std::vector<RWNode *> to_process = getNodes(graph.getEntry()->getRoot());
    std::vector<RWNode *> changed;

#ifdef DEBUG_ENABLED
    int n = 0;
#endif

    // do fixpoint
    do {
#ifdef DEBUG_ENABLED
        if (n % 100 == 0) {
            DBG(dda, "Iteration " << n << ", queued " << to_process.size() << " nodes");
        }
        ++n;
#endif
        unsigned last_processed_num = to_process.size();
        changed.clear();

        for (RWNode *cur : to_process) {
            if (processNode(cur))
                changed.push_back(cur);
        }

        if (!changed.empty()) {
            to_process.clear();
            to_process = getNodes(changed /* starting set */,
                                  last_processed_num /* expected num */);

            // since changed was not empty,
            // the to_process must not be empty too
            assert(!to_process.empty());
        }
    } while (!changed.empty());

    DBG_SECTION_END(dda, "Finished reaching definitions analysis");
}

// return the reaching definitions of ('mem', 'off', 'len')
// at the location 'where'
std::vector<RWNode *>
ReachingDefinitionsAnalysis::getDefinitions(RWNode *where, RWNode *mem,
                                            const Offset& off,
                                            const Offset& len)
{
    std::set<RWNode *> ret;
    if (mem->isUnknown()) {
        // gather all definitions of memory
        for (auto& it : where->def_map) {
            ret.insert(it.second.begin(), it.second.end());
        }
    } else {
        // gather all possible definitions of the memory
        where->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);
        where->def_map.get(mem, off, len, ret);
    }

    return std::vector<RWNode *>(ret.begin(), ret.end());
}

std::vector<RWNode *>
ReachingDefinitionsAnalysis::getDefinitions(RWNode *use) {
    std::set<RWNode *> ret;

    // gather all possible definitions of the memory including the unknown mem
    for (auto& ds : use->uses) {
        if (ds.target->isUnknown()) {
            // gather all definitions of memory
            for (auto& it : use->def_map) {
                ret.insert(it.second.begin(), it.second.end());
            }
            break; // we may bail out as we added everything
        }

        use->def_map.get(ds.target, ds.offset, ds.len, ret);
    }

    use->def_map.get(UNKNOWN_MEMORY, Offset::UNKNOWN, Offset::UNKNOWN, ret);

    return std::vector<RWNode *>(ret.begin(), ret.end());
}

} // namespace dda
} // namespace dg
