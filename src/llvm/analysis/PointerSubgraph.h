#ifndef _LLVM_DG_POINTER_SUBGRAPH_H_
#define _LLVM_DG_POINTER_SUBGRAPH_H_

#include <unordered_map>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>

#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/PointsTo/Pointer.h"

namespace dg {
namespace analysis {
namespace pta {

class LLVMPointerSubgraphBuilder
{
    const llvm::Module *M;
    const llvm::DataLayout *DL;

    // build pointer state subgraph for given graph
    // \return   root node of the graph
    PSNode *buildLLVMPointerSubgraph(const llvm::Function& F);
    std::pair<PSNode *, PSNode *> buildInstruction(const llvm::Instruction&);
    std::pair<PSNode *, PSNode *>& buildPointerSubgraphBlock(const llvm::BasicBlock& block);

    std::pair<PSNode *, PSNode *> buildArguments(const llvm::Function& F);
    std::pair<PSNode *, PSNode *> buildGlobals();

    struct Subgraph {
        Subgraph(PSNode *r1, PSNode *r2, std::pair<PSNode *, PSNode *>& a)
            : root(r1), ret(r2), args(a) {}
        Subgraph(): root(nullptr), ret(nullptr), args(nullptr, nullptr) {}

        // first and last nodes of the subgraph
        PSNode *root;
        PSNode *ret;

        // during building graph we can create some nodes as operands
        // and we don't insert them into the PointerSubgraph there, because it would
        // be difficult to get it right. We will store them here
        // and place them when we have all blocks constructed
        std::set<std::pair<PSNode *, PSNode *>> unplacedInstructions;
        // set of instructions for which we need to build uses
        // (these are ptrtoints)
        std::set<const llvm::Value *> buildUses;

        std::pair<PSNode *, PSNode *> args;
    };

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, PSNode *> nodes_map;
    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;

    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *,
             std::pair<PSNode *, PSNode *>> built_blocks;

public:
    LLVMPointerSubgraphBuilder(const llvm::Module *m)
        : M(m), DL(new llvm::DataLayout(M->getDataLayout()))
    {}

    ~LLVMPointerSubgraphBuilder()
    {
        // delete the created nodes
        for (auto it : nodes_map)
            delete it.second;

        // delete allocated memory in subgraph structures
        for (auto it : subgraphs_map) {
            delete it.second.root;
            delete it.second.ret;
        }

        delete DL;
    }

    PSNode *buildLLVMPointerSubgraph();

    // create subgraph of function @F and call+return nodes
    // to/from it
    std::pair<PSNode *, PSNode *>
    createCallToFunction(const llvm::CallInst *CInst,
                         const llvm::Function *F);

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, PSNode *>&
                                getNodesMap() const { return nodes_map; }
    PSNode *getNode(const llvm::Value *val)
    {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }

    // this is the same as the getNode, but it
    // creates ConstantExpr
    PSNode *getPointsTo(const llvm::Value *val)
    {
        PSNode *n = getNode(val);
        if (!n)
            n = getConstant(val);

        // if this is a call that returns a pointer,
        // then the points-to is in CALL_RETURN node
        if (n && (n->getType() == pta::CALL
            || n->getType() == pta::CALL_FUNCPTR))
            n = n->getPairedNode();

        return n;
    }

private:
    void addNode(const llvm::Value *val, PSNode *node)
    {
        nodes_map[val] = node;
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    bool isRelevantInstruction(const llvm::Instruction& Inst);

    PSNode *createAlloc(const llvm::Instruction *Inst);
    PSNode *createStore(const llvm::Instruction *Inst);
    PSNode *createLoad(const llvm::Instruction *Inst);
    PSNode *createGEP(const llvm::Instruction *Inst);
    PSNode *createSelect(const llvm::Instruction *Inst);
    PSNode *createPHI(const llvm::Instruction *Inst);
    PSNode *createCast(const llvm::Instruction *Inst);
    PSNode *createReturn(const llvm::Instruction *Inst);
    PSNode *createPtrToInt(const llvm::Instruction *Inst);
    PSNode *createIntToPtr(const llvm::Instruction *Inst);
    PSNode *createAsm(const llvm::Instruction *Inst);

    PSNode *createIrrelevantInst(const llvm::Value *,
                                  bool build_uses = false);
    PSNode *createIrrelevantArgument(const llvm::Argument *);
    void createIrrelevantUses(const llvm::Value *val);

    PSNode *createAdd(const llvm::Instruction *Inst);
    PSNode *createArithmetic(const llvm::Instruction *Inst);
    PSNode *createUnknown(const llvm::Instruction *Inst);

    PSNode *getOperand(const llvm::Value *val);
    PSNode *tryGetOperand(const llvm::Value *val);
    PSNode *getConstant(const llvm::Value *val);
    PSNode *createConstantExpr(const llvm::ConstantExpr *CE);
    Pointer handleConstantGep(const llvm::GetElementPtrInst *GEP);
    Pointer handleConstantBitCast(const llvm::BitCastInst *BC);
    Pointer handleConstantPtrToInt(const llvm::PtrToIntInst *P2I);
    Pointer handleConstantIntToPtr(const llvm::IntToPtrInst *I2P);
    Pointer handleConstantAdd(const llvm::Instruction *Inst);
    Pointer handleConstantArithmetic(const llvm::Instruction *Inst);
    Pointer getConstantExprPointer(const llvm::ConstantExpr *CE);

    void checkMemSet(const llvm::Instruction *Inst);
    void addPHIOperands(PSNode *node, const llvm::PHINode *PHI);
    void addPHIOperands(const llvm::Function& F);

    void addUnplacedInstructions(Subgraph& subg);
    void buildUnbuiltUses(Subgraph& subg);

    std::pair<PSNode *, PSNode *> createExtract(const llvm::Instruction *Inst);
    std::pair<PSNode *, PSNode *> createCall(const llvm::Instruction *Inst);
    std::pair<PSNode *, PSNode *> createOrGetSubgraph(const llvm::CallInst *,
                                                        const llvm::Function *);

    std::pair<PSNode *, PSNode *> createMemSet(const llvm::Instruction *);

    PSNode *handleGlobalVariableInitializer(const llvm::Constant *C,
                                             PSNode *node);
    std::pair<PSNode *, PSNode *>
    createDynamicMemAlloc(const llvm::CallInst *CInst, int type);

    std::pair<PSNode *, PSNode *>
    createRealloc(const llvm::CallInst *CInst);

    std::pair<PSNode *, PSNode *>
    createUnknownCall(const llvm::CallInst *CInst);

    std::pair<PSNode *, PSNode *>
    createIntrinsic(const llvm::Instruction *Inst);

    PSNode *createMemTransfer(const llvm::IntrinsicInst *Inst);

    std::pair<PSNode *, PSNode *>
    createVarArg(const llvm::IntrinsicInst *Inst);
};

} // namespace pta
} // namespace dg
} // namespace analysis

#endif
