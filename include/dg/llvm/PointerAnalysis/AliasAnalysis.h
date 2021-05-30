#ifndef DG_LLVM_ALIAS_ANALYSIS_H_
#define DG_LLVM_ALIAS_ANALYSIS_H_

namespace llvm {
    class Module;
    class Value;
    class DataLayout;
}

namespace dg {
namespace llvmdg {

enum class AliasResult {
    NO,
    MAY,
    MUST
};

class LLVMAliasAnalysis {
    const llvm::Module& module;

public:
    LLVMAliasAnalysis(const llvm::Module& M) : module(M) {}

    const llvm::Module &getModule() const { return module; }

    ///
    // May 'v1' and 'v2' reference the same byte in memory?
    virtual AliasResult alias(const llvm::Value * /* v1 */,
                              const llvm::Value * /* v2 */) {
        return AliasResult::MAY;
    }

    ///
    // May accessing 'b1' bytes via pointer 'v1' and 'b2' bytes via 'v2'
    // access a same byte in memory?
    virtual AliasResult access(const llvm::Value * /* v1 */,
                               const llvm::Value * /* v2 */,
                               unsigned int /* b1 */,
                               unsigned int /* b2 */) {
        return AliasResult::MAY;
    }

    ///
    // May the two instructions access the same byte in memory?
    virtual AliasResult access(const llvm::Instruction * /* I1 */,
                               const llvm::Instruction * /* I2 */) {
        return AliasResult::MAY;
    }

    ///
    // Are 'b1' bytes beginning with 'v1' a superset (supsequence) of
    // 'b2' bytes starting from 'v2'?
    virtual AliasResult covers(const llvm::Value * /* v1 */,
                               const llvm::Value * /* v2 */,
                               unsigned int /* b1 */,
                               unsigned int /* b2 */) {
        return AliasResult::MAY;
    }

    ///
    // Does instruction I1 access all the bytes accessed by I2?
    virtual AliasResult covers(const llvm::Instruction * /* I1 */,
                               const llvm::Instruction * /* I2 */) {
        return AliasResult::MAY;
    }
};

class BasicLLVMAliasAnalysis : public LLVMAliasAnalysis {
    const llvm::DataLayout& DL;

public:
    BasicLLVMAliasAnalysis(const llvm::Module& M);
    ///
    // May 'v1' and 'v2' reference the same byte in memory?
    AliasResult alias(const llvm::Value * /* v1 */,
                      const llvm::Value * /* v2 */) override;

    ///
    // May accessing 'b1' bytes via pointer 'v1' and 'b2' bytes via 'v2'
    // access a same byte in memory?
    AliasResult access(const llvm::Value * /* v1 */,
                       const llvm::Value * /* v2 */,
                       unsigned int /* b1 */,
                       unsigned int /* b2 */) override;
 
    ///
    // May the two instructions access the same byte in memory?
    AliasResult access(const llvm::Instruction * /* I1 */,
                       const llvm::Instruction * /* I2 */) override;

    ///
    // Are 'b1' bytes beginning with 'v1' a superset (supsequence) of
    // 'b2' bytes starting from 'v2'?
    AliasResult covers(const llvm::Value * /* v1 */,
                       const llvm::Value * /* v2 */,
                       unsigned int /* b1 */,
                       unsigned int /* b2 */) override;

    ///
    // Does instruction I1 access all the bytes accessed by I2?
    AliasResult covers(const llvm::Instruction * /* I1 */,
                       const llvm::Instruction * /* I2 */) override;
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_ALIAS_ANALYSIS_H_
