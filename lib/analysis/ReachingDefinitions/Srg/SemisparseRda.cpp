#include "dg/analysis/ReachingDefinitions/SemisparseRda.h"

#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h"
#include "analysis/ReachingDefinitions/Srg/SparseRDGraphBuilder.h"

#include <unordered_set>

namespace dg {
namespace analysis {
namespace rd {

using SrgBuilder = dg::analysis::rd::srg::MarkerSRGBuilderFS;
using SparseRDGraph = dg::analysis::rd::srg::SparseRDGraph;

template< typename T >
static void bfs(RDNode *from, SparseRDGraph& srg, T&& visitor) {
    std::unordered_set<RDNode *> visited;
    std::queue<RDNode *> q;
    q.push(from);
    visited.insert(from);

    while (!q.empty()) {
        RDNode *n = q.front();
        q.pop();
        auto edges_it = srg.find(n);
        if (edges_it != srg.end()) {
            auto& edges = edges_it->second;
            for (auto& edge : edges) {
                RDNode *src = edge.second;
                if (visited.insert(src).second) {
                    visitor(edge.first, edge.second);
                    q.push(src);
                }
            }
        }
        visited.insert(n);
    }
}

void SemisparseRda::run()
{
    SrgBuilder srg_builder;
    SparseRDGraph srg;

    std::unordered_set<RDNode *> to_process;
    std::tie(srg, phi_nodes) = srg_builder.build(getRoot());

    for (auto& pair : srg) {
        RDNode *dest = pair.first;
        if (dest->getUses().size() > 0 && dest->getType() != RDNodeType::PHI) {
            bfs(dest, srg, [&](DefSite& ds, RDNode *n){
                if (n->getType() != RDNodeType::PHI) {
                    merge_maps(n, dest, ds);
                }
            });
        }
    }
}

} // namespace rd
} // namespace analysis
} // namespace dg


