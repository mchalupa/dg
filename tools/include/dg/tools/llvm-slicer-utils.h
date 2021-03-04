#ifndef _DG_LLVM_SLICER_UTILS_H_
#define  _DG_LLVM_SLICER_UTILS_H_

#include <functional>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/ADT/StringRef.h>
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
#include <llvm/IR/InstIterator.h>
#endif
SILENCE_LLVM_WARNINGS_POP

std::vector<std::string> splitList(const std::string& opt, char sep = ',');

std::pair<std::vector<std::string>, std::vector<std::string>>
splitStringVector(std::vector<std::string>& vec,
                  std::function<bool(std::string&)> cmpFunc);

void replace_suffix(std::string& fl, const std::string& with);

template <typename T>
bool array_match(llvm::StringRef name, const T& names) {
    for (auto& n : names) {
        if (name.equals(n))
            return true;
    }

    return false;
}

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
namespace llvm {
class Function;
static inline iterator_range<inst_iterator> instructions(Function &F) {
    return make_range(inst_begin(F), inst_end(F));
}

static inline iterator_range<const_inst_iterator> instructions(const Function &F) {
    return make_range(inst_begin(F), inst_end(F));
}
} // llvm
#endif

// The description of a C variable
struct CVariableDecl {
    const std::string name;
    unsigned line;
    unsigned col;

    CVariableDecl(const std::string& n, unsigned l = 0, unsigned c = 0):
        name(n), line(l), col(c) {}
    CVariableDecl(CVariableDecl&&) = default;
    CVariableDecl(const CVariableDecl&) = default;
};

#endif // _DG_LLVM_SLICER_UTILS_H_
