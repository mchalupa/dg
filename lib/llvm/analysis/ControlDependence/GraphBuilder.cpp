#include "GraphBuilder.h"
#include "Function.h"
#include "Block.h"

#include "llvm/IR/Function.h"
#include <llvm/IR/CFG.h>

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

namespace dg {
namespace cd {

GraphBuilder::GraphBuilder(dg::LLVMPointerAnalysis *pointsToAnalysis)
    :pointsToAnalysis_(pointsToAnalysis) {}

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
                    handleInstruction(&llvmInst, lastBlock, createBlock, createCallReturn);
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

void GraphBuilder::handleInstruction(const llvm::Instruction *instruction, Block *lastBlock, bool &createBlock, bool &createCallReturn) {
    auto * callInst = llvm::dyn_cast<llvm::CallInst>(instruction);
    auto llvmFunctions = pointsToAnalysis_->getPointsToFunctions(callInst->getCalledValue());
    const llvm::Function * pthreadCreate = nullptr;
    const llvm::Function * pthreadJoin = nullptr;

    //TODO create method, which will return struct of 3 vectors - call functions, fork functions, join functions
    for (auto iterator = llvmFunctions.begin(); iterator != llvmFunctions.end();) {
        if ((*iterator)->getName() == "pthread_create") {
            pthreadCreate = *iterator;
            iterator = llvmFunctions.erase(iterator);
            continue;
        } else if ((*iterator)->getName() == "pthread_join") {
            pthreadJoin = *iterator;
            iterator = llvmFunctions.erase(iterator);
            continue;
        } else if ((*iterator)->size() == 0) {
            iterator = llvmFunctions.erase(iterator);
            continue;
        } else {
            ++iterator;
        }
    }

    createCallReturn |= llvmFunctions.size();

    for (auto llvmFunction : llvmFunctions) {
        auto function = createOrGetFunction(llvmFunction);
        lastBlock->addCallee(llvmFunction, function);
    }

    if (pthreadCreate) {
        createBlock = createPthreadCreate(callInst, lastBlock);
    }

    if (pthreadJoin) {
        createCallReturn = createPthreadJoin(callInst, lastBlock);
    }
}

bool GraphBuilder::createPthreadCreate(const llvm::CallInst *callInst, Block *lastBlock) {
    bool createBlock = false;
    llvm::Value * calledValue = callInst->getArgOperand(2);
    auto forkFunctions = pointsToAnalysis_->getPointsToFunctions(calledValue);
    for (auto iterator = forkFunctions.begin(); iterator != forkFunctions.end(); ++iterator) {
        if ((*iterator)->size() == 0) {
            iterator = forkFunctions.erase(iterator);
        }
    }
    createBlock |= forkFunctions.size();
    for (auto forkFunction : forkFunctions) {
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
        createCallReturn |= joinFunctions.size();
        for (auto joinFunction : joinFunctions) {
            auto llvmFunction = joinFunction->getUserData<llvm::Function>();
            auto func = createOrGetFunction(llvmFunction);
            lastBlock->addJoin(llvmFunction, func);
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
