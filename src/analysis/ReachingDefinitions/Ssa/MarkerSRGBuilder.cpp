#include "analysis/ReachingDefinitions/Ssa/MarkerSRGBuilder.h"

using namespace dg::analysis::rd::ssa;

void MarkerSRGBuilder::writeVariable(const DefSite& var, NodeT *assignment) {
    // remember the last definition
    current_def[var][assignment->getBBlock()] = assignment;
}

MarkerSRGBuilder::NodeT *MarkerSRGBuilder::readVariable(const DefSite& var, BlockT *read) {
    auto& block_defs = current_def[var];
    auto it = block_defs.find(read);
    NodeT *assignment = nullptr;

    // find the last definition
    if (it != block_defs.end()) {
        assignment = it->second;
    } else {
        assignment = readVariableRecursive(var, read);
    }
    return assignment;
}

void MarkerSRGBuilder::addPhiOperands(const DefSite& var, NodeT *phi) {
    for (BlockT *pred : phi->getBBlock()->predecessors()) {
        phi->addDef(var, true);
        phi->addUse(var);
    }
}

MarkerSRGBuilder::NodeT *MarkerSRGBuilder::readVariableRecursive(const DefSite& var, BlockT *block) {
    NodeT *val = nullptr;
    if (sealed_blocks.find(block) != sealed_blocks.end()) {
        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));
        incomplete_phis[block][var] = phi.get();
        block->prepend(phi.get());
        val = phi.get();

        phi_nodes.push_back(std::move(phi));
    } else if (block->predecessorsNum() == 1) {
        val = readVariable(var, *block->predecessors().begin());
    } else {
        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));
        block->prepend(phi.get());

        val = phi.get();
        addPhiOperands(var, val);

        phi_nodes.push_back(std::move(phi));
    }
    writeVariable(var, val);
    return val;
}
