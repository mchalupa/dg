#ifndef SEMISPARSERDA_H
#define SEMISPARSERDA_H

#include <vector>
#include <unordered_map>
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/Ssa/SparseRDGraphBuilder.h"

namespace dg {
namespace analysis {
namespace rd {

class SemisparseRda : public ReachingDefinitionsAnalysis
{
private:
    using SparseRDGraph = dg::analysis::rd::ssa::SparseRDGraph;
    SparseRDGraph& srg;

public:
    SemisparseRda(SparseRDGraph& srg, RDNode *root) : ReachingDefinitionsAnalysis(root), srg(srg) {}

    void run() override {
        std::vector<RDNode *> to_process;

        // add all sources to @to_process
        for (auto& pair : srg) {
            RDNode *source = pair.first;
            to_process.push_back(source);
        }

        // do fixpoint
        while (!to_process.empty()) {
            RDNode *source = to_process.back();
            to_process.pop_back();
            bool changed = false;
            for (auto& pair : srg[source]) {
                // variable to propagate
                DefSite *var = pair.first;
                // where to propagate
                RDNode *dest = pair.second;

                /* if (dest->def_map.update(*var, source)) { */
                if (dest->def_map.merge(&source->def_map)) {
                    to_process.push_back(dest);
                    changed = true;
                }
            }
            if (changed)
                to_process.push_back(source);

        }
    }
};

}
}
}
#endif /* SEMISPARSERDA_H */
