#ifndef DG_CDA_GRAPHBUILDER_H_
#define DG_CDA_GRAPHBUILDER_H_

#include <unordered_map>

#include "llvm/IR/CFG.h"

#include "ControlDependence/CDGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

class CDGraphBuilder {
    using CDGraph = dg::CDGraph;

    // FIXME: store this per function (i.e., Function -> (Value -> CDNode))
    std::unordered_map<const llvm::Value *, CDNode *> _nodes;
    std::unordered_map<const CDNode *, const llvm::Value *> _rev_mapping;

    CDGraph buildInstructions(const llvm::Function *F) {
        DBG_SECTION_BEGIN(cda, "Building graph (of instructions) for "
                                       << F->getName().str());

        CDGraph graph(F->getName().str());

        struct BBlock {
            std::vector<CDNode *> nodes;
        };

        std::unordered_map<const llvm::BasicBlock *, BBlock> _mapping;
        _mapping.reserve(F->size());

        // create nodes for blocks
        for (const auto &BB : *F) {
            auto it = _mapping.emplace(&BB, BBlock());
            BBlock &block = it.first->second;
            block.nodes.reserve(BB.size());
            for (const auto &I : BB) {
                auto &nd = graph.createNode();
                _rev_mapping[&nd] = &I;
                _nodes[&I] = &nd;
                block.nodes.push_back(&nd);
            }
        }

        // add successor edges
        for (const auto &BB : *F) {
            auto &bblock = _mapping[&BB];
            CDNode *last = nullptr;
            // successors inside the block
            for (auto *nd : bblock.nodes) {
                if (last)
                    graph.addNodeSuccessor(*last, *nd);
                last = nd;
            }
            assert(last || BB.empty());

            if (!last)
                continue;

            // successors between blocks
            for (const auto *bbsucc : successors(&BB)) {
                auto &succblk = _mapping[bbsucc];
                if (succblk.nodes.empty()) {
                    assert(bbsucc->empty());
                    continue;
                }

                graph.addNodeSuccessor(*last, *succblk.nodes.front());
            }
        }

        DBG_SECTION_END(cda, "Done building graph for " << F->getName().str());

        return graph;
    }

    CDGraph buildBlocks(const llvm::Function *F) {
        DBG_SECTION_BEGIN(cda, "Building graph (of blocks) for "
                                       << F->getName().str());

        CDGraph graph(F->getName().str());

        std::unordered_map<const llvm::BasicBlock *, CDNode *> _mapping;
        _mapping.reserve(F->size());
        _nodes.reserve(F->size() + _nodes.size());
        _rev_mapping.reserve(F->size() + _rev_mapping.size());

        // create nodes for blocks
        for (const auto &BB : *F) {
            auto &nd = graph.createNode();
            _mapping[&BB] = &nd;
            _nodes[&BB] = &nd;
            _rev_mapping[&nd] = &BB;
        }

        // add successor edges
        for (const auto &BB : *F) {
            auto *nd = _mapping[&BB];
            assert(nd && "BUG: creating nodes for bblocks");

            for (const auto *bbsucc : successors(&BB)) {
                auto *succ = _mapping[bbsucc];
                assert(succ && "BUG: do not have a bblock created");
                graph.addNodeSuccessor(*nd, *succ);
            }
        }

        DBG_SECTION_END(cda, "Done building graph for function "
                                     << F->getName().str());

        return graph;
    }

  public:
    // \param instructions  true if we should build nodes for the instructions
    //                      instead of for basic blocks?
    CDGraph build(const llvm::Function *F, bool instructions = false) {
        if (instructions) {
            return buildInstructions(F);
        }

        return buildBlocks(F);
    }

    CDNode *getNode(const llvm::Value *v) {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    const CDNode *getNode(const llvm::Value *v) const {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    const llvm::Value *getValue(const CDNode *n) const {
        auto it = _rev_mapping.find(n);
        return it == _rev_mapping.end() ? nullptr : it->second;
    }
};

} // namespace llvmdg
} // namespace dg

#endif
