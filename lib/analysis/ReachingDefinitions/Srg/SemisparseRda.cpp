#include "dg/analysis/ReachingDefinitions/SemisparseRda.h"

#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h"
#include "analysis/ReachingDefinitions/Srg/SparseRDGraphBuilder.h"

#include <unordered_set>

namespace dg {
namespace analysis {
namespace rd {

void SemisparseRda::run()
{
    using SrgBuilder = dg::analysis::rd::srg::MarkerSRGBuilderFS;
    using SparseRDGraph = dg::analysis::rd::srg::SparseRDGraph;
    SrgBuilder srg_builder;
    SparseRDGraph srg;

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

} // namespace rd
} // namespace analysis
} // namespace dg


