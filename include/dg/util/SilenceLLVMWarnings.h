#ifndef DG_SILENCE_LLVM_WARNINGS_H
#define DG_SILENCE_LLVM_WARNINGS_H

// Helper macros to ignore unrelated warnings in LLVM libraries

#ifndef SILENCE_LLVM_WARNINGS_PUSH
#if defined(_MSC_VER)
#define SILENCE_LLVM_WARNINGS_PUSH                          \
_Pragma("warning(push)");
// TODO
#elif defined(__clang__)
#define SILENCE_LLVM_WARNINGS_PUSH                          \
_Pragma("clang diagnostic push");                           \
_Pragma("clang diagnostic ignored \"-Wunused-parameter\"");
#else // GCC
#define SILENCE_LLVM_WARNINGS_PUSH                          \
_Pragma("GCC diagnostic push");                             \
_Pragma("GCC diagnostic ignored \"-Wunused-parameter\"");
#endif
#endif

#ifndef SILENCE_LLVM_WARNINGS_POP
#if defined(_MSC_VER)
#define SILENCE_LLVM_WARNINGS_POP \
_Pragma("warning(pop)");
#elif defined(__clang__)
#define SILENCE_LLVM_WARNINGS_POP \
_Pragma("clang diagnostic pop");
#else // GCC
#define SILENCE_LLVM_WARNINGS_POP \
_Pragma("GCC diagnostic pop");
#endif
#endif

#endif // DG_SILENCE_LLVM_WARNINGS_H