#ifndef DG_CDA_GRAPHBUILDER_H_
#define DG_CDA_GRAPHBUILDER_H_

#include <unordered_map>

#include "llvm/IR/CFG.h"

#include "CDGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

class CDGraphBuilder {
    CDGraph buildInstructions(const llvm::Function *F) {

        DBG_SECTION_BEGIN(cda, "Building graph (of instructions) for " << F->getName().str());

        CDGraph graph(F->getName().str());

        struct BBlock {
            std::vector<CDNode *> nodes;
        };

        std::unordered_map<const llvm::BasicBlock *, BBlock> _mapping;
        _mapping.reserve(F->size());

        // create nodes for blocks
        for (auto& BB : *F) {
            auto it = _mapping.emplace(&BB, BBlock());
            BBlock& block = it.first->second;
            block.nodes.reserve(BB.size());
            for (auto& I : BB) {
                (void)I;
                auto& nd = graph.createNode();
                block.nodes.push_back(&nd);
            }
        }

        // add successor edges
        for (auto& BB : *F) {
            auto& bblock = _mapping[&BB];
            CDNode *last = nullptr;
            // successors inside the block
            for (auto* nd : bblock.nodes) {
                if (last)
                    last->addSuccessor(nd);
                last = nd;
            }
            assert(last || BB.size() == 0);

            if (!last)
                continue;

            // successors between blocks
            for (auto *bbsucc : successors(&BB)) {
                auto& succblk = _mapping[bbsucc];
                if (succblk.nodes.empty()) {
                    assert(bbsucc->size() == 0);
                    continue;
                }

                last->addSuccessor(succblk.nodes.front());
            }
        }

        DBG_SECTION_END(cda, "Done building graph for " << F->getName().str());

        return graph;
    }

    CDGraph buildBlocks(const llvm::Function *F) {

        DBG_SECTION_BEGIN(cda, "Building graph (of blocks) for " << F->getName().str());

        CDGraph graph(F->getName().str());

        std::unordered_map<const llvm::BasicBlock *, CDNode *> _mapping;
        _mapping.reserve(F->size());

        // create nodes for blocks
        for (auto& BB : *F) {
            auto& nd = graph.createNode();
            _mapping[&BB] = &nd;
        }

        // add successor edges
        for (auto& BB : *F) {
            auto *nd = _mapping[&BB];
            assert(nd && "BUG: creating nodes for bblocks");

            for (auto *bbsucc : successors(&BB)) {
                auto *succ = _mapping[bbsucc];
                assert(succ && "BUG: do not have a bblock created");
                nd->addSuccessor(succ);
            }
        }

        DBG_SECTION_END(cda, "Done building graph for function " << F->getName().str());

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
};

} // namespace llvmdg
} // namespace dg

#endif
