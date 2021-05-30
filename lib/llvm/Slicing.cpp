#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/llvm/LLVMFastSlicer.h"
#include "dg/ADT/Queue.h"


namespace dg {
namespace llvmdg {

struct Data {
    // set of top-level values that we add to the slice when we hit them
    std::set<llvm::Value *> search_values{};
    // set of instructions for which we must find their defining write
    std::set<llvm::Value *> search_defs{};
};

class SlicerWorker {
    std::set<llvm::Value *> slice;
    ADT::HashMap<llvm::Instruction *, Data> data;
    ADT::QueueFIFO<llvm::Instruction *> queue;

    bool addTopLevelValuesToSlice(llvm::Value *val) {
        bool changed = false;
        ADT::QueueFIFO<llvm::Value *> tmpqueue;
        tmpqueue.push(val);

        while (!tmpqueue.empty()) {
            auto *cur = tmpqueue.pop();
            if (!slice.insert(cur).second)
                continue;

            changed = true;
            auto *curI = llvm::dyn_cast<llvm::Instruction>(cur);
            if (!curI)
                continue;
            for (auto *op : curI->operands()) {
                tmpqueue.push(op);
            }
        }
        return changed;
    }
};

std::set<llvm::Value *>
LLVMFastSlicer::computeSlice(const std::vector<const llvm::Value *> &criteria) {
    SlicerWorker slicer;
    for (auto *c : criteria) {
        slicer.addTopLevelValuesToSlice(c);
    }

    changed = false;
    while (!queue.empty()) {
        auto *I = queue.pop();
    }

    return slice;
}

void LLVMFastSlicer::sliceModule(const std::set<llvm::Value *> &slice) {
}

} // namespace llvmdg
} // namespace dg
