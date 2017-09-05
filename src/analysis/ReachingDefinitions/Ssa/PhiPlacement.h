
#ifndef _DG_PHIPLACEMENT_H_
#define _DG_PHIPLACEMENT_H_

#include "BBlock.h"
#include "analysis/ReachingDefinitions/Ssa/AssignmentFinder.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {

using PhiAdditions = std::unordered_map<BBlock<RDNode> *, std::vector<DefSite>>;

/**
 * Calculates where phi-functions for variables should be placed to create SSA form
 * Prerequisites:
 * + Dominance Frontiers calculated on BBlock-s
 * + Assignment Map
 */
class PhiPlacement
{
private:
    using RDBlock = BBlock<RDNode>;

public:
    PhiAdditions calculate(AssignmentMap&& am) const
    {
        PhiAdditions result;
        for (auto& def : am) {
            // DomFronPlus
            std::set<RDBlock *> dfp;
            std::vector<RDNode *> w = std::move(def.second);
            std::set<RDNode *> work(w.begin(), w.end());

            while (!w.empty()) {
                RDNode *n = w.back();
                w.pop_back();
                RDBlock *X = n->getBBlock();

                for (RDBlock* Y : X->getDomFrontiers()) {
                    if (dfp.find(Y) == dfp.end()) {

                        for (RDNode *N : Y->getNodes()) {
                            for (const DefSite& cds : N->getDefines()) {
                                if (cds.target == def.first) {
                                    result[Y].push_back(cds);
                                }
                            }
                            for (const DefSite& cds : N->getUses()) {
                                if (cds.target == def.first) {
                                    result[Y].push_back(cds);
                                }
                            }
                        }

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

    /**
     * Return ownership of added phi-nodes
     */
    std::vector<std::unique_ptr<RDNode>> place(const PhiAdditions& pa) const
    {
        std::vector<std::unique_ptr<RDNode>> result;
        for (auto& pair : pa) {
            RDBlock *target = pair.first;
            // assumption: target->getFirstNode() does not manipulate any var that is in pair.second
            RDNode *last = target->getFirstNode();
            for (auto& var : pair.second) {
                RDNode *node = new RDNode(RDNodeType::PHI);
                node->addDef(var, true);
                node->addUse(var);
                result.push_back(std::unique_ptr<RDNode>(node));
                node->insertAfter(last);
                last = node;
                // order of nodes in block will be different but it is not big deal with phi nodes
                target->prepend(node);
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
