#ifndef DG_FORK_JOIN_ANALYSIS_H_
#define DG_FORK_JOIN_ANALYSIS_H_

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

namespace dg {

///
// Analyse which functions are spawned by threads
// and which threads are joined by joins.
class ForkJoinAnalysis {
    LLVMPointerAnalysis *_PTA{nullptr};
    // const llvm::Module *_M{nullptr};

  public:
    ForkJoinAnalysis( // const llvm::Module *M,
            LLVMPointerAnalysis *PTA)
            : _PTA(PTA) /*, _M(M)*/ {};

    /// Take llvm::Value which is a call to pthread_join
    //  and return a vector of values that (may) spawn a thread
    //  that may be joined by this join.
    std::vector<const llvm::Value *> matchJoin(const llvm::Value * /*joinVal*/);

    /// Take llvm::Value which is a call to pthread_join
    //  and return a vector of functions that may have been joined
    //  by this join.
    std::vector<const llvm::Value *>
    joinFunctions(const llvm::Value * /*joinVal*/);
};

} // namespace dg

#endif
