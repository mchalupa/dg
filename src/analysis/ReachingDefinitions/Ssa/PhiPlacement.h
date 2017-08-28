
#ifndef _DG_PHIPLACEMENT_H_
#define _DG_PHIPLACEMENT_H_

#include "BBlock.h"
#include "analysis/ReachingDefinitions/Ssa/DefUse.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {

using PhiAdditions = std::unordered_map<BBlock<RDNode> *, std::vector<RDNode *>>;

/**
 * Calculates where phi-functions for variables should be placed to create SSA form
 * Prerequisites:
 * + Dominance Frontiers calculated on BBlock-s
 * + Def->Use graph
 */
class PhiPlacement
{
private:
    using RDBlock = BBlock<RDNode>;

public:
    PhiAdditions calculate(DefUseGraph&& dug) const
    {
        PhiAdditions result;
        for (auto& def : dug) {
            // DomFronPlus
            std::set<RDBlock *> dfp;
            std::vector<RDNode *> w = std::move(def.second);
            std::set<RDNode *> work(w.begin(), w.end());

            while (!w.empty()) {
                RDNode *n = w.back();
                w.pop_back();
                RDBlock *X = n->getBBlock();

                for (RDBlock* Y : X->getDomFrontiers()) {
                    // TODO: Y->getFirstNode()
                    if (dfp.find(Y) == dfp.end()) {
                        result[Y].push_back(def.first);
                        dfp.insert(Y);
                        if (work.find(Y->getFirstNode()) == work.end()) {
                            work.insert(Y->getFirstNode());
                            w.push_back(Y->getFirstNode());
                        }
                    }
                }
            }
        }
        return result;
    }
};

// namespaces
}
} 
}
}
#endif /* _DG_PHIPLACEMENT_H_ */
