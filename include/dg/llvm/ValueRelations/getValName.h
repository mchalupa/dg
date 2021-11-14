#ifndef DG_LLVM_GET_VAL_NAME_H_
#define DG_LLVM_GET_VAL_NAME_H_

#include <fstream>
#include <iostream>
#include <llvm/Support/raw_os_ostream.h>
#include <sstream>
#include <string>

namespace dg {
namespace debug {
inline std::string getValName(const llvm::Value *val) {
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    std::string result = ostr.str();
    size_t quote = result.find("\"");
    while (quote != std::string::npos) {
        result.replace(quote, 1, "\\\"");
        quote = result.find("\"", quote + 2);
    }
    size_t start = result.find_first_not_of(" ");
    assert(start != std::string::npos);

    // break the string if it is too long
    return result.substr(start);
}

inline std::string getTypeName(const llvm::Type *type) {
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(type);
#if LLVM_VERSION_MAJOR <= 4
    ro << *const_cast<llvm::Type *>(type);
#else
    ro << *type;
#endif
    ro.flush();

    std::string result = ostr.str();
    size_t start = result.find_first_not_of(" ");
    assert(start != std::string::npos);

    // break the string if it is too long
    return result.substr(start);
}

} // namespace debug
} // namespace dg

#endif // DG_LLVM_GET_VAL_NAME_H_
