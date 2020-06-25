#ifndef DG_CDA_GRAPHBUILDER_H_
#define DG_CDA_GRAPHBUILDER_H_

#include "llvm/GraphBuilder.h"
#include "dg/BBlockBase.h"

namespace dg {
namespace llvmdg {

class CDNodeBase {
};

class CDBBlockBase {
};

class CDFunctionBase {
};

class CDGraphBuilder : public GraphBuilder<CDNodeBase, CDBBlockBase, CDFunctionBase> {

    NodesSeq<CDNodeBase> createNode(const llvm::Value *) override {}
    CDBBlockBase& createBBlock(const llvm::BasicBlock *, CDFunctionBase&) override {}
    CDFunctionBase& createSubgraph(const llvm::Function *) override {}
public:
    CDGraphBuilder(const llvm::Module *module) : GraphBuilder(module) {}
    virtual ~CDGraphBuilder() {}
};

//////////////////////////////////////////////////////////////////
/// Graph that contains as nodes basic blocks
//////////////////////////////////////////////////////////////////
class CDBBlock;
class CDBBlockFunction : public CDFunctionBase {

};

class CDBBlock : public CDBBlockBase, public BBlockBase<CDBBlock> {

};

class CDBlocksGraphBuilder : public CDGraphBuilder {
    NodesSeq<CDNodeBase> createNode(const llvm::Value *) override {}
    CDBBlockBase& createBBlock(const llvm::BasicBlock *, CDFunctionBase&) override {}
    CDFunctionBase& createSubgraph(const llvm::Function *) override {}
};


//////////////////////////////////////////////////////////////////
/// Graph that contains as nodes instructions
//////////////////////////////////////////////////////////////////
class CDNode;
class CDNodeFunction : public CDFunctionBase {

};

// inherit also from BBlock base, we need only the successors
class CDNode : public CDNodeBase, public BBlockBase<CDNode> {

};

class CDNodeBBlock : public CDBBlockBase {
    // the add successor method is going to add edge between nodes,
    // not between blocks

};

class CDNodesGraphBuilder : public CDGraphBuilder {

    NodesSeq<CDNodeBase> createNode(const llvm::Value *) override {}
    CDBBlockBase& createBBlock(const llvm::BasicBlock *, CDFunctionBase&) override {}
    CDFunctionBase& createSubgraph(const llvm::Function *) override {}
};


} // namespace llvmdg
} // namespace dg

#endif
