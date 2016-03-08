#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include "analysis/Pointer.h"
#include <unordered_map>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

namespace dg {
namespace analysis {
namespace pss {

class PSS;
class PSSNode;

class LLVMPSSBuilder
{
    const llvm::Module *M;
    const llvm::DataLayout *DL;

    // build pointer state subgraph for given graph
    // \return   root node of the graph
    PSSNode *buildLLVMPSS(const llvm::Function& F);
    std::pair<PSSNode *, PSSNode *> buildPSSBlock(const llvm::BasicBlock& block);

    std::pair<PSSNode *, PSSNode *> buildArguments(const llvm::Function& F);
    std::pair<PSSNode *, PSSNode *> buildGlobals();

    struct Subgraph {
        Subgraph(PSSNode *r1, PSSNode *r2, std::pair<PSSNode *, PSSNode *>& a)
            : root(r1), ret(r2), args(a) {}
        Subgraph() {memset(this, 0, sizeof *this);}
        
        PSSNode *root;
        PSSNode *ret;
        std::pair<PSSNode *, PSSNode *> args;
    };

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, PSSNode *> nodes_map;
    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;

public:
    LLVMPSSBuilder(const llvm::Module *m)
        : M(m), DL(new llvm::DataLayout(M->getDataLayout()))
    {}

    ~LLVMPSSBuilder()
    {
        delete DL;
    }

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, PSSNode *>&
                                getNodesMap() const { return nodes_map; }

    PSSNode *buildLLVMPSS()
    {
        // get entry function
        llvm::Function *F = M->getFunction("main");
        if (!F) {
            llvm::errs() << "Need main function in module\n";
            abort();
        }

        // first we must build globals, because nodes can use them as operands
        std::pair<PSSNode *, PSSNode *> glob = buildGlobals();

        // now we can build rest of the graph
        PSSNode *root = buildLLVMPSS(*F);

        // do we have any globals at all? If so, insert them at the begining of the graph
        // FIXME: we do not need to process them later, should we do it somehow differently?
        // something like 'static nodes' in PSS...
        if (glob.first) {
            assert(glob.second && "Have the start but not the end");

            // this is a sequence of global nodes, make it the root of the graph
            glob.second->addSuccessor(root);
            root = glob.first;
        }

        return root;
    }

private:
    PSSNode *createAlloc(const llvm::Instruction *Inst);
    PSSNode *createStore(const llvm::Instruction *Inst);
    PSSNode *createLoad(const llvm::Instruction *Inst);
    PSSNode *createGEP(const llvm::Instruction *Inst);
    PSSNode *createCast(const llvm::Instruction *Inst);
    PSSNode *createReturn(const llvm::Instruction *Inst);

    PSSNode *getOperand(const llvm::Value *val);
    PSSNode *getConstant(const llvm::Value *val);
    PSSNode *createConstantExpr(const llvm::ConstantExpr *CE);
    Pointer handleConstantGep(const llvm::GetElementPtrInst *GEP);
    Pointer handleConstantBitCast(const llvm::BitCastInst *BC);
    Pointer getConstantExprPointer(const llvm::ConstantExpr *CE);

    std::pair<PSSNode *, PSSNode *> createCall(const llvm::Instruction *Inst);
    std::pair<PSSNode *, PSSNode *> createOrGetSubgraph(const llvm::CallInst *CInst,
                                                        const llvm::Function *F);

    PSSNode *handleGlobalVariableInitializer(const llvm::Constant *C,
                                             PSSNode *node);
    std::pair<PSSNode *, PSSNode *>
    createDynamicMemAlloc(const llvm::CallInst *CInst, int type);
};


} // namespace pss
} // namespace dg
} // namespace analysis

#endif
