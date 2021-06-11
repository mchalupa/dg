#include "dg/llvm/SystemDependenceGraph/SDG2Dot.h"
#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"

std::ostream &operator<<(std::ostream &os, const llvm::Value *val) {
    if (!val) {
        os << "(null)";
        return os;
    }

    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (llvm::isa<llvm::Function>(val)) {
        ro << "FUNC " << val->getName();
    } else if (const auto *B = llvm::dyn_cast<llvm::BasicBlock>(val)) {
        ro << B->getParent()->getName() << "::\n";
        ro << "label " << val->getName();
    } else if (const auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
        const auto *const B = I->getParent();
        if (B) {
            ro << B->getParent()->getName() << "::\n";
        } else {
            ro << "<null>::\n";
        }
        ro << *val;
    } else {
        ro << *val;
    }

    ro.flush();

    // break the string if it is too long
    std::string str = ostr.str();
    if (str.length() > 100) {
        str.resize(40);
    }

    // escape the "
    size_t pos = 0;
    while ((pos = str.find('"', pos)) != std::string::npos) {
        str.replace(pos, 1, "\\\"");
        // we replaced one char with two, so we must shift after the new "
        pos += 2;
    }

    os << str;

    return os;
}
