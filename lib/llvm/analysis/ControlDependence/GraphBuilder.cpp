#include "GraphBuilder.h"
#include "Function.h"
#include "Block.h"
#include "TarjanAnalysis.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

namespace dg {
namespace cd {

GraphBuilder::GraphBuilder(dg::LLVMPointerAnalysis *pointsToAnalysis)
    :pointsToAnalysis_(pointsToAnalysis), threads(pointsToAnalysis->threads()) {}

GraphBuilder::~GraphBuilder() {
    for (auto function : functions_) {
        delete function.second;
    }
}

Function *GraphBuilder::buildFunctionRecursively(const llvm::Function *llvmFunction) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto iterator = functions_.find(llvmFunction);
    if (iterator != functions_.end()) {
        return nullptr;
    }
    iterator = functions_.emplace(llvmFunction, new Function()).first;
    Function * function = iterator->second;

    std::map<const llvm::Instruction *, Block *> instToBlockMap;
    Block * lastBlock = nullptr;
    for (auto & llvmBlock : *llvmFunction) {
        if (isReachable(&llvmBlock)) {
            bool createBlock = true;
            for (auto & llvmInst : llvmBlock) {
                if (createBlock) {
                    auto tmpBlock = new Block();
                    function->addBlock(tmpBlock);
                    if (lastBlock) {
                        if (lastBlock->llvmBlock() == &llvmBlock) {
                            lastBlock->addSuccessor(tmpBlock);
                        }
                    }
                    lastBlock = tmpBlock;
                    createBlock = false;
                }
                bool createCallReturn = false;
                if (llvmInst.getOpcode() == llvm::Instruction::Call) {
                    handleCallInstruction(&llvmInst, lastBlock, createBlock, createCallReturn);
                }
                lastBlock->addInstruction(&llvmInst);
                instToBlockMap.emplace(&llvmInst, lastBlock);

                if (createCallReturn) {
                    auto tmpBlock = new Block();
                    function->addBlock(tmpBlock);
                    lastBlock->addSuccessor(tmpBlock);
                    lastBlock = tmpBlock;
                    createBlock = true;
                }
            }
        }
    }

    for (auto & llvmBlock : *llvmFunction) {
        if (isReachable(&llvmBlock)) {
            auto block = instToBlockMap[&llvmBlock.back()];
            for (auto succ = llvm::succ_begin(&llvmBlock); succ != llvm::succ_end(&llvmBlock); ++succ) {
                auto succ_block = instToBlockMap[&succ->front()];
                block->addSuccessor(succ_block);
            }
            if (successorsNumber(&llvmBlock) == 0) {
                block->addSuccessor(function->exit());
            }
        }
    }

    TarjanAnalysis<Block> tarjan(function->nodes().size());
    tarjan.compute(function->entry());
    tarjan.computeCondensation();
    auto components = tarjan.computeBackWardReachability(function->exit());

    for (auto component : components) {
        if (component->nodes().size() > 1 ||
            component->successors().find(component) != component->successors().end()) {
            component->nodes().back()->addSuccessor(function->exit());
        }
    }

    return function;
}

Function *GraphBuilder::findFunction(const llvm::Function *llvmFunction) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto iterator = functions_.find(llvmFunction);
    if (iterator != functions_.end()) {
        return iterator->second;
    }
    return nullptr;
}

Function *GraphBuilder::createOrGetFunction(const llvm::Function *llvmFunction) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto function = findFunction(llvmFunction);
    if (!function) {
        function = buildFunctionRecursively(llvmFunction);
    }
    return  function;
}

std::map<const llvm::Function *, Function *> GraphBuilder::functions() const {
    return functions_;
}

void GraphBuilder::dumpNodes(std::ostream &ostream) const {
    for (auto function : functions_) {
        function.second->dumpBlocks(ostream);
    }

}

void GraphBuilder::dumpEdges(std::ostream &ostream) const {
    for (auto function : functions_) {
        function.second->dumpEdges(ostream);
    }
}

void GraphBuilder::dump(std::ostream &ostream) const {
    ostream << "digraph \"BlockGraph\" {\n";
    dumpNodes(ostream);
    dumpEdges(ostream);
    ostream << "}\n";
}

void GraphBuilder::handleCallInstruction(const llvm::Instruction *instruction, Block *lastBlock, bool &createBlock, bool &createCallReturn) {
    auto * callInst = llvm::dyn_cast<llvm::CallInst>(instruction);
    auto llvmFunctions = pointsToAnalysis_->getPointsToFunctions(callInst->getCalledValue());

    for (auto llvmFunction : llvmFunctions) {
        if (llvmFunction->size() > 0) {
            auto function = createOrGetFunction(llvmFunction);
            lastBlock->addCallee(llvmFunction, function);
        } else if (threads) {
            if (llvmFunction->getName() == "pthread_create") {
                createBlock |= createPthreadCreate(callInst, lastBlock);
            } else if (llvmFunction->getName() == "pthread_join") {
                createCallReturn |= createPthreadJoin(callInst, lastBlock);
            }
        }
    }
}

bool GraphBuilder::createPthreadCreate(const llvm::CallInst *callInst, Block *lastBlock) {
    bool createBlock = false;
    llvm::Value * calledValue = callInst->getArgOperand(2);
    auto forkFunctions = pointsToAnalysis_->getPointsToFunctions(calledValue);
    std::vector <const llvm::Function *> forkFunctionsWithBlock;
    for (auto forkFunction : forkFunctions) {
        if (forkFunction->size() > 0) {
            forkFunctionsWithBlock.push_back(forkFunction);
        }
    }
    createBlock |= forkFunctionsWithBlock.size();
    for (auto forkFunction : forkFunctionsWithBlock) {
        auto function = createOrGetFunction(forkFunction);
        lastBlock->addFork(forkFunction, function);
    }
    return createBlock;
}

bool GraphBuilder::createPthreadJoin(const llvm::CallInst *callInst, Block *lastBlock) {
    bool createCallReturn = false;
    auto PSJoin = pointsToAnalysis_->findJoin(callInst);
    if (PSJoin) {
        auto joinFunctions = PSJoin->functions();
        for (auto joinFunction : joinFunctions) {
            auto llvmFunction = joinFunction->getUserData<llvm::Function>();
            if (llvmFunction->size() > 0) {
                auto func = createOrGetFunction(llvmFunction);
                lastBlock->addJoin(llvmFunction, func);
                createCallReturn |= true;
            }
        }
    }
    return createCallReturn;
}

int predecessorsNumber(const llvm::BasicBlock *basicBlock) {
    return static_cast<int>(pred_size(basicBlock));
}

int successorsNumber(const llvm::BasicBlock *basicBlock) {
    return static_cast<int>(succ_size(basicBlock));
}

bool isReachable(const llvm::BasicBlock *basicBlock) {
    return predecessorsNumber(basicBlock) > 0 ||
           &basicBlock->getParent()->front() == basicBlock;
}

}
}
