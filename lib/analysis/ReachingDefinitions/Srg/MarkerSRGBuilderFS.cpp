#include "analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h"

using namespace dg::analysis::rd::srg;
/**
 * Saves the current definition of certain variable in given block
 * Used from value numbering procedures.
 */
void MarkerSRGBuilderFS::writeVariableStrong(const DefSite& var, NodeT *assignment, BlockT *block) {
    detail::Interval interval = concretize(detail::Interval{var.offset, var.len}, var.target->getSize());
    current_weak_def[var.target][block].killOverlapping(interval);
    current_def[var.target][block].killOverlapping(interval);
    // remember the last definition
    current_def[var.target][block].add(std::move(interval), assignment);
}

void MarkerSRGBuilderFS::writeVariableWeak(const DefSite& var, NodeT *assignment, BlockT *block) {
    current_weak_def[var.target][block].add(concretize(detail::Interval{var.offset, var.len}, var.target->getSize()), assignment);
}

std::vector<MarkerSRGBuilderFS::NodeT *> MarkerSRGBuilderFS::readVariable(const DefSite& var, BlockT *read, BlockT *start, const Intervals& covered) {
    assert( read );
    // use specialized method for unknown memory
    if (var.target == UNKNOWN_MEMORY) {
        std::unordered_map<NodeT *, detail::DisjointIntervalSet> found;
        return std::vector<NodeT *> { readUnknown(read, found) };
    }

    auto& block_defs = current_def[var.target];
    auto it = block_defs.find(read);
    std::vector<NodeT *> result;
    const auto interval = concretize(detail::Interval{var.offset, var.len}, var.target->getSize());

    // find weak defs
    auto block_weak_defs = current_weak_def[var.target][read].collectAll(interval);
    auto unknown_defs = current_weak_def[UNKNOWN_MEMORY][read].collectAll(interval);

    // find the last definition
    if (it != block_defs.end()) {
        Intervals cov;
        bool is_covered = false;
        std::tie(result, cov, is_covered) = it->second.collect(interval, covered);
        if (!is_covered && (!interval.isUnknown() || read != start)) {
            NodeT *phi = readVariableRecursive(var, read, start, cov);
            result.push_back(phi);
        }
    } else {
        result.push_back(readVariableRecursive(var, read, start, covered));
    }

    // add weak defs & unknown weak defs
    std::move(block_weak_defs.begin(), block_weak_defs.end(), std::back_inserter(result));
    std::move(unknown_defs.begin(), unknown_defs.end(), std::back_inserter(result));

    return result;
}

MarkerSRGBuilderFS::NodeT *MarkerSRGBuilderFS::addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block, BlockT *start, const std::vector<detail::Interval>& covered) {

    const auto interval = concretize(detail::Interval{var.offset, var.len}, var.target->getSize());

    phi->addDef(var, true);
    phi->addUse(var);

    for (BlockT *pred : block->predecessors()) {
        std::vector<NodeT *> assignments;
        Intervals cov;
        bool is_covered = false;

        std::tie(assignments, cov, is_covered) = last_def[var.target][pred].collect(interval, covered);
        // add weak updates
        auto weak_defs = last_weak_def[var.target][pred].collectAll(interval);
        std::move(weak_defs.begin(), weak_defs.end(), std::back_inserter(assignments));

        if (!is_covered || (interval.isUnknown() && block != start)) {
            std::vector<NodeT *> assignments2 = readVariable(var, pred, start, cov);
            std::move(assignments2.begin(), assignments2.end(), std::back_inserter(assignments));
        }

        for (auto& assignment : assignments)
            insertSrgEdge(assignment, phi, var);
    }

    return tryRemoveTrivialPhi(phi);
}

MarkerSRGBuilderFS::NodeT* MarkerSRGBuilderFS::tryRemoveTrivialPhi(NodeT *phi) {
    auto operands = srg.find(phi);
    // is @phi undef?
    if (operands == srg.end()) {
        return phi;
    }

    NodeT *same = nullptr;
    // is phi node non-trivial?
    for (auto& edge : operands->second) {
         NodeT* dest = edge.second;
        if (dest == same || dest == phi) {
            continue;
        } else if (same != nullptr) {
            return phi;
        }
        same = dest;
    }

    if (same == nullptr) {
        // the phi is unreachable or in the start block
        return phi;
    }

    replacePhi(phi, same);

    auto users_it = reverse_srg.find(phi);
    if (users_it == reverse_srg.end()) {
        // no users...
        return phi;
    }

    auto users = users_it->second;
    for (auto& edge : users) {
        NodeT* user = edge.second;
        if (user != phi && user->getType() == RDNodeType::PHI) {
            tryRemoveTrivialPhi(user);
        }
    }

    return same;
}

void MarkerSRGBuilderFS::replacePhi(NodeT *phi, NodeT *replacement) {
    // the purpose of this method is to reroute definitions to uses
    auto uses_it = reverse_srg.find(phi);

    if (uses_it == reverse_srg.end() || uses_it->second.size() == 0) {
        // there is nothing to transplant
        return;
    }

    auto defs_it = srg.find(phi);
    if (defs_it != srg.end()) {
        auto& defs = defs_it->second;
        for (auto& def_edge : defs) {
            DefSite& var = def_edge.first;
            NodeT *dest = def_edge.second;
            removeSrgEdge(phi, dest, var);
        }
    }

    auto uses = uses_it->second;

    for (auto& use_edge : uses) {
        DefSite var = use_edge.first;
        NodeT *dest = use_edge.second;
        removeSrgEdge(dest, phi, var);
        insertSrgEdge(dest, replacement, var);
    }
}

MarkerSRGBuilderFS::NodeT *MarkerSRGBuilderFS::readVariableRecursive(const DefSite& var, BlockT *block, BlockT *start, const std::vector<detail::Interval>& covered) {
    std::vector<NodeT *> result;

    auto interval = concretize(detail::Interval{var.offset, var.len}, var.target->getSize());
    auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));

    phi->setBasicBlock(block);
    // writeVariableStrong kills current weak definitions, which are needed in the phi node, so we need to lookup them first.
    auto weak_defs = current_weak_def[var.target][block].collectAll(interval);
    for (auto& assignment : weak_defs)
        insertSrgEdge(assignment, phi.get(), var);

    writeVariableStrong(var, phi.get(), block);
    NodeT *val = addPhiOperands(var, phi.get(), block, start, covered);
    writeVariableStrong(var, val, block);

    phi_nodes.push_back(std::move(phi));

    return val;
}

/*
  Find relevant definitions of all variables and join them into a single phi node.
  Only search until all variables are 'covered' or an allocation is found.
  Branching will be solved via phi nodes
*/
MarkerSRGBuilderFS::NodeT *MarkerSRGBuilderFS::readUnknown(BlockT *read, std::unordered_map<NodeT *, detail::DisjointIntervalSet>& found) {
     std::vector<NodeT *> result;

    // try to find definitions of UNKNOWN_MEMORY in the current block.
    auto& block_defs = current_def[UNKNOWN_MEMORY];
    auto it = block_defs.find(read);
    const auto interval = detail::Interval{Offset::UNKNOWN, Offset::UNKNOWN};

    // does any definition exist in this block?
    if (it != block_defs.end()) {
        Intervals cov;
        bool is_covered = false;
        result = it->second.collectAll(interval);

        // no phi necessary for single definition
        if (result.size() == 1) {
            return *result.begin();
        } else {
            std::unique_ptr<NodeT> phi{new NodeT(RDNodeType::PHI)};
            NodeT *ptr = phi.get();
            phi->setBasicBlock(read);
            writeVariableStrong(UNKNOWN_MEMORY, phi.get(), read);
            for (auto& node : result) {
                insertSrgEdge(node, phi.get(), UNKNOWN_MEMORY);
            }
            phi_nodes.push_back(std::move(phi));

            return ptr;
        }
    }
    // otherwise, we need to traverse the entire program
    // find definitions in current block
    for (auto& var_blocks : current_def) {
        // we do not care which variable it is -- we are searching for definitions of all variables
        auto& var = var_blocks.second;
        // TODO: if not already covered(@var)
        // second thought: the coverage check cost might be too high
        for (auto& block_defs : var_blocks.second) {
            if (block_defs.first == read) {
                for (auto& interval_val : block_defs.second) {
                    // now we are iterating over the interval map
                    result.push_back(interval_val.second);
                    // TODO: only if @interval_val.second is a strong update, add coverage information
                }
            }
        }
    }

    // find weak definitions in current block
    for (auto& var_blocks : current_weak_def) {
        // we do not care which variable it is -- we are searching for all definitions of all variables
        for (auto& block_defs : var_blocks.second) {
            if (block_defs.first == read) {
                for (auto& interval_val : block_defs.second) {
                    // now we are iterating the interval map
                    result.push_back(interval_val.second);
                }
            }
        }
    }


    // if not phi node necessary
    if (result.size() == 1) {
        return *result.begin();
    }

    // make a phi node for unknown memory
    auto phi = std::unique_ptr<NodeT>(new NodeT(RDNodeType::PHI));
    phi->setBasicBlock(read);
    writeVariableStrong(UNKNOWN_MEMORY, phi.get(), read);
    // continue the search for definitions in previous blocks
    for (auto& pred : read->predecessors()) {
        if (pred == read)
            continue;

        auto assignment = readUnknown(pred, found);
        result.push_back(assignment);
    }
    for (auto& node : result) {
        insertSrgEdge(node, phi.get(), UNKNOWN_MEMORY);
    }

    NodeT *ptr = phi.get();
    phi_nodes.push_back(std::move(phi));

    // TODO: return also coverage information
    return ptr;
}
