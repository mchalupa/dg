#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include <unordered_map>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>

#include "analysis/PointsTo/PSS.h"
#include "analysis/PointsTo/Pointer.h"

namespace dg {
namespace analysis {
namespace pss {

class LLVMPSSBuilder
{
    const llvm::Module *M;
    const llvm::DataLayout *DL;

    // build pointer state subgraph for given graph
    // \return   root node of the graph
    PSSNode *buildLLVMPSS(const llvm::Function& F);
    std::pair<PSSNode *, PSSNode *> buildInstruction(const llvm::Instruction&);
    std::pair<PSSNode *, PSSNode *>& buildPSSBlock(const llvm::BasicBlock& block);

    std::pair<PSSNode *, PSSNode *> buildArguments(const llvm::Function& F);
    std::pair<PSSNode *, PSSNode *> buildGlobals();

    struct Subgraph {
        Subgraph(PSSNode *r1, PSSNode *r2, std::pair<PSSNode *, PSSNode *>& a)
            : root(r1), ret(r2), args(a) {}
        Subgraph() {memset(this, 0, sizeof *this);}

        // first and last nodes of the subgraph
        PSSNode *root;
        PSSNode *ret;

        // during building graph we can create some nodes as operands
        // and we don't insert them into the PSS there, because it would
        // be difficult to get it right. We will store them here
        // and place them when we have all blocks constructed
        std::set<std::pair<PSSNode *, PSSNode *>> unplacedInstructions;
        // set of instructions for which we need to build uses
        // (these are ptrtoints)
        std::set<const llvm::Value *> buildUses;

        std::pair<PSSNode *, PSSNode *> args;
    };

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, PSSNode *> nodes_map;
    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;

    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *,
             std::pair<PSSNode *, PSSNode *>> built_blocks;

public:
    LLVMPSSBuilder(const llvm::Module *m)
        : M(m), DL(new llvm::DataLayout(M->getDataLayout()))
    {}

    ~LLVMPSSBuilder()
    {
        delete DL;
    }

    PSSNode *buildLLVMPSS();

    // create subgraph of function @F and call+return nodes
    // to/from it
    std::pair<PSSNode *, PSSNode *>
    createCallToFunction(const llvm::CallInst *CInst,
                         const llvm::Function *F);

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, PSSNode *>&
                                getNodesMap() const { return nodes_map; }
    PSSNode *getNode(const llvm::Value *val)
    {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }

    // this is the same as the getNode, but it
    // creates ConstantExpr
    PSSNode *getPointsTo(const llvm::Value *val)
    {
        PSSNode *n = getNode(val);
        if (!n)
            n = getConstant(val);

        // if this is a call that returns a pointer,
        // then the points-to is in CALL_RETURN node
        if (n && (n->getType() == pss::CALL
            || n->getType() == pss::CALL_FUNCPTR))
            n = n->getPairedNode();

        return n;
    }

private:
    void addNode(const llvm::Value *val, PSSNode *node)
    {
        nodes_map[val] = node;
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    bool isRelevantInstruction(const llvm::Instruction& Inst);

    PSSNode *createAlloc(const llvm::Instruction *Inst);
    PSSNode *createStore(const llvm::Instruction *Inst);
    PSSNode *createLoad(const llvm::Instruction *Inst);
    PSSNode *createGEP(const llvm::Instruction *Inst);
    PSSNode *createSelect(const llvm::Instruction *Inst);
    PSSNode *createPHI(const llvm::Instruction *Inst);
    PSSNode *createCast(const llvm::Instruction *Inst);
    PSSNode *createReturn(const llvm::Instruction *Inst);
    PSSNode *createPtrToInt(const llvm::Instruction *Inst);
    PSSNode *createIntToPtr(const llvm::Instruction *Inst);
    PSSNode *createAsm(const llvm::Instruction *Inst);

    PSSNode *createIrrelevantInst(const llvm::Value *,
                                  bool build_uses = false);
    PSSNode *createIrrelevantArgument(const llvm::Argument *);
    void createIrrelevantUses(const llvm::Value *val);

    PSSNode *createAdd(const llvm::Instruction *Inst);
    PSSNode *createArithmetic(const llvm::Instruction *Inst);
    PSSNode *createUnknown(const llvm::Instruction *Inst);

    PSSNode *getOperand(const llvm::Value *val);
    PSSNode *tryGetOperand(const llvm::Value *val);
    PSSNode *getConstant(const llvm::Value *val);
    PSSNode *createConstantExpr(const llvm::ConstantExpr *CE);
    Pointer handleConstantGep(const llvm::GetElementPtrInst *GEP);
    Pointer handleConstantBitCast(const llvm::BitCastInst *BC);
    Pointer getConstantExprPointer(const llvm::ConstantExpr *CE);

    void checkMemSet(const llvm::Instruction *Inst);
    void addPHIOperands(PSSNode *node, const llvm::PHINode *PHI);
    void addPHIOperands(const llvm::Function& F);

    void addUnplacedInstructions(Subgraph& subg);
    void buildUnbuiltUses(Subgraph& subg);

    std::pair<PSSNode *, PSSNode *> createCall(const llvm::Instruction *Inst);
    std::pair<PSSNode *, PSSNode *> createOrGetSubgraph(const llvm::CallInst *,
                                                        const llvm::Function *);

    std::pair<PSSNode *, PSSNode *> createMemSet(const llvm::Instruction *);

    PSSNode *handleGlobalVariableInitializer(const llvm::Constant *C,
                                             PSSNode *node);
    std::pair<PSSNode *, PSSNode *>
    createDynamicMemAlloc(const llvm::CallInst *CInst, int type);

    std::pair<PSSNode *, PSSNode *>
    createRealloc(const llvm::CallInst *CInst);

    std::pair<PSSNode *, PSSNode *>
    createUnknownCall(const llvm::CallInst *CInst);

    std::pair<PSSNode *, PSSNode *>
    createIntrinsic(const llvm::Instruction *Inst);

    PSSNode *createMemTransfer(const llvm::IntrinsicInst *Inst);

    std::pair<PSSNode *, PSSNode *>
    createVarArg(const llvm::IntrinsicInst *Inst);
};

} // namespace pss
} // namespace dg
} // namespace analysis

#endif
