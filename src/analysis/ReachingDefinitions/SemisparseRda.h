#ifndef _DG_SEMISPARSERDA_H_
#define _DG_SEMISPARSERDA_H_

#include <vector>
#include <queue>
#include <unordered_map>
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/Srg/SparseRDGraphBuilder.h"
#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h"

namespace dg {
namespace analysis {
namespace rd {

class SemisparseRda : public ReachingDefinitionsAnalysis
{
private:
    using SrgBuilder = dg::analysis::rd::srg::MarkerSRGBuilderFS;
    using SparseRDGraph = dg::analysis::rd::srg::SparseRDGraph;

    bool merge_maps(RDNode *source, RDNode *dest, DefSite& var) {
        bool changed = false;

        if (source->getType() != RDNodeType::PHI)
            changed |= dest->def_map.add(var, source);

        for (auto& pair : source->def_map) {
            const DefSite& ds = pair.first;
            auto& nodes = pair.second;

            if (ds.target == var.target) {
                for (RDNode *node : nodes) {
                    if (node->getType() != RDNodeType::PHI)
                        changed |= dest->def_map.add(ds, node);
                }
            }
        }
        return changed;
    }

    SrgBuilder srg_builder;
    SparseRDGraph srg;
    std::vector<std::unique_ptr<RDNode>> phi_nodes;

    template< typename T >
    void bfs(RDNode *from, T&& visitor) {
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

public:
    SemisparseRda(RDNode *root) : ReachingDefinitionsAnalysis(root) {}

    void run() override {
        std::unordered_set<RDNode *> to_process;
        std::tie(srg, phi_nodes) = srg_builder.build(root);

        for (auto& pair : srg) {
            RDNode *dest = pair.first;
            if (dest->getUses().size() > 0 && dest->getType() != RDNodeType::PHI) {
                bfs(dest, [&](DefSite& ds, RDNode *n){
                    if (n->getType() != RDNodeType::PHI) {
                        merge_maps(n, dest, ds);
                    }
                });
            }
        }
    }
};

}
}
}
#endif /* _DG_SEMISPARSERDA_H_ */
