#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include "dg/llvm/LLVMFastSlicer.h"
#include "dg/ADT/Queue.h"
#include "dg/ADT/HashMap.h"


namespace dg {
namespace llvmdg {

struct Data {
    // set of top-level values that we add to the slice when we hit them
    std::set<llvm::Value *> search_values{};
    // set of instructions for which we must find their defining write
    std::set<llvm::Value *> search_defs{};
};

class SlicerImpl {
    std::set<llvm::Value *> slice;
    dg::HashMap<llvm::Instruction *, Data> data;
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
            for (auto &op : curI->operands()) {
                tmpqueue.push(op.get());
            }
        }
        return changed;
    }

public:
    std::set<llvm::Value *>
    computeSlice(const std::vector<const llvm::Value *> &criteria) {
        for (auto *c : criteria) {
            addTopLevelValuesToSlice(const_cast<llvm::Value*>(c));
        }

        /*
        changed = false;
        while (!queue.empty()) {
            auto *I = queue.pop();
        }
        */

        return slice;
    }
};

std::set<llvm::Value *>
LLVMFastSlicer::computeSlice(const std::vector<const llvm::Value *> &criteria) {
    return SlicerImpl().computeSlice(criteria);
}

void LLVMFastSlicer::sliceModule(const std::set<llvm::Value *> &slice) {
    for (auto *val : slice) {
        llvm::errs() << "IN SLICE: " << *val << "\n";
    }
}

} // namespace llvmdg
} // namespace dg
