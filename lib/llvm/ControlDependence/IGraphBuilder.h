#ifndef DG_CDA_IGRAPHBUILDER_H_
#define DG_CDA_IGRAPHBUILDER_H_

#include <unordered_map>

#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/CallGraph/CallGraph.h"

#include "ControlDependence/CDGraph.h"
#include "GraphBuilder.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

namespace {
    const llvm::Instruction *
    getNextNonDebugInstruction(const llvm::Instruction *I) {
#if LLVM_VERSION_MAJOR >= 7
    return I->getNextNonDebugInstruction();
#else
    // this is the implementation of Instruction::getNextNonDebugInstruction()
    // from LLVM 12 (adjusted)
    for (const auto *NI = I->getNextNode(); NI; NI = NI->getNextNode())
      if (!llvm::isa<llvm::DbgInfoIntrinsic>(NI))
        return NI;
    return nullptr;
#endif
    }
}

///
// Whole-program graph for interprocedural analysis.
// Note that control dependencies include separate interprocedural analysis
// that fills in interprocedural CD into intraprocedural result.
// But this is another way how to do that.... (not used by default)
class ICDGraphBuilder {
    using CDGraph = dg::CDGraph;

    struct CallInfo {
        // called functions
        std::vector<const llvm::Function *> funs;

        CallInfo(std::vector<const llvm::Function *>&& f) : funs(std::move(f)) {}
        CallInfo(CallInfo&&) = default;
        CallInfo(const CallInfo&) = delete;
    };

    std::unordered_map<const llvm::Value *, CDNode *> _nodes;
    std::unordered_map<const CDNode *, const llvm::Value *> _rev_mapping;
    std::map<const llvm::CallInst *, CallInfo> calls;

    LLVMPointerAnalysis *_pta{nullptr};
    CallGraph *_cg{nullptr};

    std::vector<const llvm::Function *> getCalledFunctions(const llvm::CallInst *C) {
        auto *f = C->getCalledFunction();
        if (f) {
            if (f->isDeclaration()) {
                return {};
            }
            return {f};
        }

        // function pointer call
        if (_pta) {
            abort();
            return {};
        }
        if (_cg) {
            abort();
            return {};
        }

        return {};
    }

    static const llvm::Value *_getEntryNode(const llvm::Function *f) {
        assert(f && "Got no function");
        auto &B = f->getEntryBlock();
        return &*(B.begin());
    }

    CDGraph buildInstructions(const llvm::Module *M) {
        DBG_SECTION_BEGIN(cda, "Building ICFG (of instructions) for the whole module");
        CDGraph graph("ICFG");

        for (auto& F : *M) {
            buildInstructions(graph, F);
        }

        // add interprocedural edges
        for (auto &it : calls) {
            auto *C = it.first;
            // call edge
            for (auto *f : it.second.funs) {
                auto *cnode = getNode(C);
                assert(cnode && "Do not have the node for a call");
                auto *entrynode = getNode(_getEntryNode(f));
                assert(entrynode);
                graph.addNodeSuccessor(*cnode, *entrynode);

                // return edges
                auto *retsite = getNextNonDebugInstruction(C);
                assert(retsite);
                auto *retsitenode = getNode(retsite);
                assert(retsitenode);
                for (auto& B : *f) {
                    if (auto *R = llvm::dyn_cast<llvm::ReturnInst>(B.getTerminator())) {
                        auto *rnode = getNode(R);
                        graph.addNodeSuccessor(*rnode, *retsitenode);
                    }
                }
            }
        }

        DBG_SECTION_END(cda, "Done building interprocedural CD graph");

        return graph;
    }

    void buildInstructions(CDGraph& graph, const llvm::Function& F) {
        struct BBlock {
            std::vector<CDNode *> nodes;
        };

        std::unordered_map<const llvm::BasicBlock *, BBlock> _mapping;
        //_mapping.reserve(F->size());

        // create nodes for instructions
        for (auto& BB : F) {
            auto it = _mapping.emplace(&BB, BBlock());
            BBlock& block = it.first->second;
            block.nodes.reserve(BB.size());
            for (auto& I : BB) {
                if (auto *C = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    auto funs = getCalledFunctions(C);
                    if (!funs.empty())
                        calls.emplace(C, CallInfo(std::move(funs)));
                }

                auto& nd = graph.createNode();
                _rev_mapping[&nd] = &I;
                _nodes[&I] = &nd;
                block.nodes.push_back(&nd);
            }
        }

        // add intraprocedural successor edges
        for (auto& BB : F) {
            auto& bblock = _mapping[&BB];
            CDNode *last = nullptr;
            // successors inside the block
            for (auto* nd : bblock.nodes) {
                if (last)
                    graph.addNodeSuccessor(*last, *nd);
                auto *C = llvm::dyn_cast<llvm::CallInst>(getValue(nd));
                if (C && (calls.find(C) != calls.end())) {
                    // if the node is a call that calls some defined functions
                    // (we store only such in calls), its successor
                    // is going to be the entry to the procedure
                    last = nullptr;
                } else {
                    last = nd;
                }
            }
            // every block has at least one (terminator) instruction
            assert(last && "Empty block");

            // successors between blocks
            for (auto *bbsucc : successors(&BB)) {
                auto& succblk = _mapping[bbsucc];
                if (succblk.nodes.empty()) {
                    assert(bbsucc->size() == 0);
                    continue;
                }

                graph.addNodeSuccessor(*last, *succblk.nodes.front());
            }
        }
    }

    CDGraph buildBlocks(const llvm::Module *M) {
        DBG_SECTION_BEGIN(cda, "Building ICFG (of blocks) for the whole module");
        CDGraph graph("ICFG");

        for (auto& F : *M) {
            buildBlocks(graph, F);
        }

        // add successor edges
        for (auto &F : *M) {
            for (auto& BB : F) {
                auto *nd = getNode(&BB);
                assert(nd && "BUG: creating nodes for bblocks");
                auto *blknd = nd;

                // add interprocedural edges
                for (auto& I : BB) {
                    auto *C = llvm::dyn_cast<llvm::CallInst>(&I);
                    if (!C) {
                        continue;
                    }

                    // create a block that represents the rest of the block
                    // and add an edge from the returns of f
                    const auto& funs = getCalledFunctions(C);
                    if (funs.empty())
                        continue;

                    auto& retsite = graph.createNode();

                    // call inst
                    for (auto *f : getCalledFunctions(C)) {
                        auto *entrynode = getNode(&f->getEntryBlock());
                        assert(entrynode);
                        graph.addNodeSuccessor(*blknd, *entrynode);

                        // return edges
                        for (auto& B : *f) {
                            if (llvm::isa<llvm::ReturnInst>(B.getTerminator())) {
                                // the block returns
                                auto *rnode = getNode(&B);
                                assert(rnode);
                                graph.addNodeSuccessor(*rnode, retsite);
                            }
                        }
                    }
                    blknd = &retsite;
                }

                assert(blknd);
                // add intraprocedural edges
                for (auto *bbsucc : successors(&BB)) {
                    auto *succ = getNode(bbsucc);
                    assert(succ && "BUG: do not have a bblock created");
                    graph.addNodeSuccessor(*blknd, *succ);
                }
            }
        }

        return graph;
    }

    void buildBlocks(CDGraph& graph, const llvm::Function& F) {
        DBG_SECTION_BEGIN(cda, "Building ICFG (of blocks) for " << F.getName().str());

        // create nodes for blocks
        for (auto& BB : F) {
            auto& nd = graph.createNode();
            _nodes[&BB] = &nd;
            _rev_mapping[&nd] = &BB;
        }

        DBG_SECTION_END(cda, "Done building graph for function " << F.getName().str());
    }

public:

    ICDGraphBuilder(LLVMPointerAnalysis *pta = nullptr, CallGraph *cg = nullptr):
        _pta(pta), _cg(cg) {}

    // \param instructions  true if we should build nodes for the instructions
    //                      instead of for basic blocks?
    CDGraph build(const llvm::Module *M, bool instructions = false) {
        if (instructions) {
            return buildInstructions(M);
        }

        return buildBlocks(M);
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
