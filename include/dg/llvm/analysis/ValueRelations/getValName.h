#ifndef _DG_LLVM_GET_VAL_NAME_H_
#define _DG_LLVM_GET_VAL_NAME_H_

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <llvm/Support/raw_os_ostream.h>

namespace dg{
namespace debug {
inline std::string getValName(const llvm::Value *val) {
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

} // namespace debug
} // namespace dg

#endif // _DG_LLVM_GET_VAL_NAME_H_
