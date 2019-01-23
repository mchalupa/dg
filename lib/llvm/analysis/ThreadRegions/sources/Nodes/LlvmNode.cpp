
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <sstream>

#include "Node.h"

using namespace std;
using namespace llvm;

LlvmNode::LlvmNode(ControlFlowGraph *controlFlowGraph,
                   const llvm::Value *value):Node(controlFlowGraph),
                                             llvmValue_(value) {}

std::string LlvmNode::dump() const {
    stringstream stream;
    string llvmTemporaryString;
    raw_string_ostream llvmStream(llvmTemporaryString);
    llvmValue_->print(llvmStream);

    stream << this->dotName() << " [label=\"<" << this->id() << "> " << llvmTemporaryString << "\"]\n";

    return stream.str();
}

const Value *LlvmNode::llvmValue() const {
    return llvmValue_;
}
