#ifndef _DG_LLVM_SLICER_UTILS_H_
#define  _DG_LLVM_SLICER_UTILS_H_

#include <functional>

#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/ADT/StringRef.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

std::vector<std::string> splitList(const std::string& opt, char sep = ',');

std::pair<std::vector<std::string>, std::vector<std::string>>
splitStringVector(std::vector<std::string>& vec,
                  std::function<bool(std::string&)> cmpFunc);

void replace_suffix(std::string& fl, const std::string& with);

bool array_match(llvm::StringRef name, const char *names[]);

template <typename T>
bool array_match(llvm::StringRef name, const T& names) {
    for (auto& n : names) {
        if (name.equals(n))
            return true;
    }

    return false;
}

#endif // _DG_LLVM_SLICER_UTILS_H_
