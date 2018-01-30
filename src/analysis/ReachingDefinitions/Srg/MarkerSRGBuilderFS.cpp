#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h"

using namespace dg::analysis::rd::srg;

/*
 * If the interval has unknown offset or length, it is changed to contain everything
 */
static detail::Interval concretize(detail::Interval interval) {
    if (interval.isUnknown()) {
        return detail::Interval{ 0, ~( (uint64_t) 0 ) };
    }
    return interval;
}

void MarkerSRGBuilderFS::writeVariable(const DefSite& var, NodeT *assignment, BlockT *block) {
    // remember the last definition
    current_def[var.target][block].add(concretize(detail::Interval{var.offset, var.len}), assignment);
}

std::vector<MarkerSRGBuilderFS::NodeT *> MarkerSRGBuilderFS::readVariable(const DefSite& var, BlockT *read, const Intervals& covered) {
    assert( read );

    auto& block_defs = current_def[var.target];
    auto it = block_defs.find(read);
    std::vector<NodeT *> result;
    const auto interval = concretize(detail::Interval{var.offset, var.len});

    // find the last definition
    if (it != block_defs.end()) {
        Intervals cov;
        bool is_covered = false;
        std::tie(result, cov, is_covered) = it->second.collect(interval, covered);
        if (!is_covered && !interval.isUnknown()) {
            NodeT *phi = readVariableRecursive(var, read, cov);
            result.push_back(phi);
        }
    } else {
        result.push_back(readVariableRecursive(var, read, covered));
    }
    return result;
}

void MarkerSRGBuilderFS::addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block, const std::vector<detail::Interval>& covered) {

    phi->addDef(var, true);
    phi->addUse(var);

    for (BlockT *pred : block->predecessors()) {
        std::vector<NodeT *> assignments;
        Intervals cov;
        bool is_covered = false;
        std::tie(assignments,cov,is_covered) = last_def[var.target][pred].collect(detail::Interval{var.offset, var.len}, covered);
        if (!is_covered) {
            std::vector<NodeT *> assignments2 = readVariable(var, pred, cov);
            assignments.insert(assignments.begin(), assignments2.begin(), assignments2.end());
        }
        for (auto& assignment : assignments)
            insertSrgEdge(assignment, phi, var);
    }
}

MarkerSRGBuilderFS::NodeT *MarkerSRGBuilderFS::readVariableRecursive(const DefSite& var, BlockT *block, const std::vector<detail::Interval>& covered) {
    std::vector<NodeT *> result;
    bool is_covered = false;

    NodeT *val = nullptr;
    if (block->predecessorsNum() == 1) {
        BlockT *predBB = *(block->predecessors().begin());
        Intervals cov;

        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));
        phi->addDef(var, true);
        phi->addUse(var);
        phi->setBasicBlock(block);
        writeVariable(var, phi.get(), block);

        std::vector<NodeT *> assignments;
        std::tie(assignments, cov, is_covered) = last_def[var.target][predBB].collect(concretize(detail::Interval{var.offset,var.len}), covered);
        if (!is_covered) {
            std::vector<NodeT *> assignments2 = readVariable(var, predBB, cov);
            assignments.insert(assignments.begin(), assignments2.begin(), assignments2.end());
        }
        for (NodeT *node : assignments) {
            insertSrgEdge(node, phi.get(), var);
        }
        val = phi.get();
        phi_nodes.push_back(std::move(phi));

    } else {
        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));

        phi->setBasicBlock(block);
        writeVariable(var, phi.get(), block);
        addPhiOperands(var, phi.get(), block, covered);

        val = phi.get();
        phi_nodes.push_back(std::move(phi));
    }
    writeVariable(var, val, block);
    return val;
}
