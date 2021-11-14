#ifndef DG_LLVM_VALUE_RELATION_STRUCTURE_ELEMENTS_H_
#define DG_LLVM_VALUE_RELATION_STRUCTURE_ELEMENTS_H_

#include <llvm/IR/Value.h>

#include "GraphElements.h"

namespace dg {
namespace vr {

struct AllocatedSizeView {
    const llvm::Value *elementCount = nullptr;
    uint64_t elementSize = 0; // in bytes

    AllocatedSizeView() = default;
    AllocatedSizeView(const llvm::Value *count, uint64_t size)
            : elementCount(count), elementSize(size) {}
};

class AllocatedArea {
    const llvm::Value *ptr;
    // used only if memory was allocated with realloc, as fallback when realloc
    // fails
    const llvm::Value *reallocatedPtr = nullptr;
    AllocatedSizeView originalSizeView;

  public:
    static const llvm::Value *stripCasts(const llvm::Value *inst);

    static uint64_t getBytes(const llvm::Type *type);

    AllocatedArea(const llvm::AllocaInst *alloca);

    AllocatedArea(const llvm::CallInst *call);

    const llvm::Value *getPtr() const { return ptr; }
    const llvm::Value *getReallocatedPtr() const { return reallocatedPtr; }

    std::vector<AllocatedSizeView> getAllocatedSizeViews() const;

#ifndef NDEBUG
    void ddump() const;
#endif
};

struct CallRelation {
    std::vector<std::pair<const llvm::Argument *, const llvm::Value *>>
            equalPairs;
    VRLocation *callSite = nullptr;
};

struct Precondition {
    const llvm::Argument *arg;
    Relations::Type rel;
    const llvm::Value *val;

    Precondition(const llvm::Argument *a, Relations::Type r,
                 const llvm::Value *v)
            : arg(a), rel(r), val(v) {}
};

struct BorderValue {
    size_t id;
    const llvm::Argument *from;
    const llvm::Value *stored;

    BorderValue(size_t i, const llvm::Argument *f, const llvm::Value *s)
            : id(i), from(f), stored(s) {}
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_STRUCTURE_ELEMENTS_H_
