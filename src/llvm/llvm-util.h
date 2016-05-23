#ifndef _DG_LLVM_UTIL_H_
#define _DG_LLVM_UTIL_H_

#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

namespace dg {
namespace llvmutil {

using namespace llvm;

void
print(const Value *val,
      raw_ostream& os,
      const char *prefix=nullptr,
      bool newline = false)
{
    if (prefix)
        os << prefix;

    if (isa<Function>(val))
        os << val->getName().data();
    else
        os << *val;

    if (newline)
        os << "\n";
}

void
printerr(const char *msg, const Value *val, bool newline = true)
{
    print(val, errs(), msg, newline);
}

} // namespace llvmutil
} // namespace dg

#endif // _DG_LLVM_UTIL_H_
