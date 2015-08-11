/// XXX add licence
//

#ifdef HAVE_LLVM

#ifndef _LLVM_NODE_H_
#define _LLVM_NODE_H_

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

typedef dg::BBlock<LLVMNode> LLVMBBlock;
typedef dg::DGParameter<LLVMNode> LLVMDGParameter;
typedef dg::DGParameters<const llvm::Value *, LLVMNode> LLVMDGParameters;

/// ------------------------------------------------------------------
//  -- LLVMNode
/// ------------------------------------------------------------------
class LLVMNode : public dg::Node<LLVMDependenceGraph,
                                 const llvm::Value *, LLVMNode>
{
public:
    LLVMNode(const llvm::Value *val)
        :dg::Node<LLVMDependenceGraph, const llvm::Value *, LLVMNode>(val),
         operands(nullptr), operands_num(0)
    {}

    ~LLVMNode()
    {
        delete[] operands;
    }

    const llvm::Value *getValue() const
    {
        return getKey();
    }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    // XXX create new class for parameters
    void addActualParameters(LLVMDependenceGraph *);

    LLVMNode **getOperands()
    {
        if (!operands)
            findOperands();

        return operands;
    }

    LLVMNode *getOperand(unsigned int idx)
    {
        if (!operands)
            findOperands();

        assert(operands_num > idx && "idx is too high");
        return operands[idx];
    }

private:
    LLVMNode **findOperands();
    // here we can store operands of instructions so that
    // finding them will be asymptotically constant
    LLVMNode **operands;
    size_t operands_num;
};

} // namespace dg

#endif // _LLVM_NODE_H_

#endif /* HAVE_LLVM */
