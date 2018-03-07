#ifndef _DG_SEMISPARSERDA_H_
#define _DG_SEMISPARSERDA_H_

#include <vector>
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

public:
    SemisparseRda(RDNode *root) : ReachingDefinitionsAnalysis(root) {}

    void run() override {
        std::unordered_set<RDNode *> to_process;
        std::tie(srg, phi_nodes) = srg_builder.build(root);

        // add all sources to @to_process
        for (auto& pair : srg) {
            RDNode *source = pair.first;
            to_process.insert(source);
        }

        // do fixpoint
        while (!to_process.empty()) {
            auto it = to_process.begin();

            RDNode *source = *it;
            to_process.erase(it);

            for (auto& pair : srg[source]) {
                // variable to propagate
                DefSite var = pair.first;
                // where to propagate
                RDNode *dest = pair.second;

                if (merge_maps(source, dest, var)) {
                    // if dest does not define this variable, it is unnecessary to process it again
                    if (dest->defines(var.target)) {
                        to_process.insert(dest);
                    }
                }
            }
        }
    }
};

}
}
}
#endif /* _DG_SEMISPARSERDA_H_ */
