#ifndef _LLVM_DG_DEBUG_H_
#define _LLVM_DG_DEBUG_H_

#ifdef DEBUG_ENABLED

#ifdef DBG
#undef DBG
#endif

#define DBG(...)                                        \
    do {                                                \
        errs() << __FILE__ << ":" << __LINE__ << " ";   \
        errs() << __VA_ARGS__;                          \
        errs() << "\n";                                 \
    } while(0)

#else

#define DBG(...) do { } while (0)

#endif // DEBUG_ENABLED

#endif // _LLVM_DG_DEBUG_H_
