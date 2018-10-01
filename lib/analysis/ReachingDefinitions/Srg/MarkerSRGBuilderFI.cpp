#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.h"

using namespace dg::analysis::rd::srg;

void MarkerSRGBuilderFI::writeVariable(const DefSite& var, NodeT *assignment, BlockT *block) {
    // remember the last definition
    current_def[var.target][block] = assignment;
}

MarkerSRGBuilderFI::NodeT *MarkerSRGBuilderFI::readVariable(const DefSite& var, BlockT *read) {
    assert( read );
    auto& block_defs = current_def[var.target];
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

void MarkerSRGBuilderFI::addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block) {

    phi->addDef(var, true);
    phi->addUse(var);

    for (BlockT *pred : block->predecessors()) {
        NodeT *assignment = nullptr;
        assignment = last_def[var.target][pred];
        if (!assignment)
            assignment = readVariable(var, pred);
        insertSrgEdge(assignment, phi, var);
    }
}

MarkerSRGBuilderFI::NodeT *MarkerSRGBuilderFI::readVariableRecursive(const DefSite& var, BlockT *block) {
    NodeT *val = nullptr;
    if (block->predecessorsNum() == 1) {
        BlockT *predBB = *(block->predecessors().begin());
        val = last_def[var.target][predBB];
        if (!val)
            val = readVariable(var, predBB);
    } else {
        auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));

        phi->setBasicBlock(block);
        writeVariable(var, phi.get(), block);
        addPhiOperands(var, phi.get(), block);

        val = phi.get();
        phi_nodes.push_back(std::move(phi));
    }
    writeVariable(var, val, block);
    return val;
}
