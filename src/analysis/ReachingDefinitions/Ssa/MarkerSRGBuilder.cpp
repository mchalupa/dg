#include "analysis/ReachingDefinitions/Ssa/MarkerSRGBuilder.h"

using namespace dg::analysis::rd::ssa;

void MarkerSRGBuilder::writeVariable(const DefSite& var, NodeT *assignment, BlockT *block) {
    // remember the last definition
    current_def[var][block] = assignment;
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

void MarkerSRGBuilder::addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block) {

    phi->addDef(var, true);
    phi->addUse(var);

    for (BlockT *pred : block->predecessors()) {
        NodeT *last_def = readVariable(var, pred);
        if (last_def) {
            insertSrgEdge(last_def, phi, var);
        }
    }
}

MarkerSRGBuilder::NodeT *MarkerSRGBuilder::readVariableRecursive(const DefSite& var, BlockT *block) {
    NodeT *val = nullptr;
    if (block->predecessorsNum() == 0)
        return nullptr;
    else if (block->predecessorsNum() == 1) {
        val = readVariable(var, *block->predecessors().begin());
    } else {
        auto operandless_phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));
        val = operandless_phi.get();

        writeVariable(var, val, block);
        phi_nodes.push_back(std::move(operandless_phi));

        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));

        val = phi.get();
        addPhiOperands(var, val, block);

        phi_nodes.push_back(std::move(phi));
    }
    if (val) {
        writeVariable(var, val, block);
    }
    return val;
}
