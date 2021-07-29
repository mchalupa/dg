#ifndef DG_LLVMDG2DOT_H_
#define DG_LLVMDG2DOT_H_

#include <iostream>
#include <ostream>
#include <sstream>
#include <string>

#include "dg/DG2Dot.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 4))
#include "llvm/DebugInfo.h" //DIScope
#else
#include "llvm/IR/DebugInfo.h" //DIScope
#endif

using namespace dg;
namespace dg {
namespace debug {

/*
static std::ostream& operator<<(std::ostream& os, const analysis::Offset& off)
{
    if (off.offset == Offset::UNKNOWN)
        os << "UNKNOWN";
    else
        os << off.offset;

    return os;
}
*/

namespace {
inline std::ostream &printLLVMVal(std::ostream &os, const llvm::Value *val) {
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
} // anonymous namespace

class LLVMDG2Dot : public debug::DG2Dot<LLVMNode> {
  public:
    // FIXME: make dg const
    LLVMDG2Dot(LLVMDependenceGraph *dg,
               uint32_t opts = debug::PRINT_CFG | debug::PRINT_DD |
                               debug::PRINT_CD,
               const char *file = nullptr)
            : debug::DG2Dot<LLVMNode>(dg, opts, file) {}

    /* virtual */
    std::ostream &printKey(std::ostream &os, llvm::Value *val) override {
        return printLLVMVal(os, val);
    }

    /* virtual */
    bool checkNode(std::ostream &os, LLVMNode *node) override {
        bool err = false;
        const llvm::Value *val = node->getKey();

        if (!val) {
            os << "\\nERR: no value in node";
            return true;
        }

        if (!node->getBBlock() && !llvm::isa<llvm::Function>(val) &&
            !llvm::isa<llvm::GlobalVariable>(val)) {
            err = true;
            os << "\\nERR: no BB";
        }

        // Print Location in source file. Print it only for LLVM 3.6 and higher.
        // The versions before 3.6 had different API, so this is quite
        // a workaround, not a real fix. If anybody needs this functionality
        // on those versions, fix this :)
        if (const llvm::Instruction *I =
                    llvm::dyn_cast<llvm::Instruction>(val)) {
            const llvm::DebugLoc &Loc = I->getDebugLoc();
#if ((LLVM_VERSION_MAJOR > 3) ||                                               \
     ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR > 6)))
            if (Loc) {
                os << "\" labelURL=\"";
                llvm::raw_os_ostream ross(os);
                Loc.print(ross);
#else
            if (Loc.getLine() > 0) {
                os << "\" labelURL=\"";
                llvm::raw_os_ostream ross(os);
                // Loc.print(I->getParent()->getContext(), ross);
                const llvm::DebugLoc *tmpLoc = &Loc;
                int nclosingBrack = 0;
                while (tmpLoc) {
                    llvm::DIScope Scope(
                            tmpLoc->getScope(I->getParent()->getContext()));
                    ross << Scope.getFilename();
                    ross << ':' << tmpLoc->getLine();
                    if (tmpLoc->getCol() != 0)
                        ross << ':' << tmpLoc->getCol();

                    llvm::MDNode *inlineMN =
                            tmpLoc->getInlinedAt(I->getParent()->getContext());
                    if (inlineMN) {
                        llvm::DebugLoc InlinedAtDL =
                                llvm::DebugLoc::getFromDILocation(inlineMN);
                        if (!InlinedAtDL.isUnknown()) {
                            ross << " @[ ";
                            tmpLoc = &InlinedAtDL;
                            nclosingBrack++;
                        } else {
                            tmpLoc = nullptr;
                        }
                    } else {
                        tmpLoc = nullptr;
                    }
                }
                while (nclosingBrack > 0) {
                    ross << " ]";
                    nclosingBrack--;
                }
#endif
                ross.flush();
            }
        }

        return err;
    }

    bool dump(const char *new_file = nullptr,
              const char *dump_func_only = nullptr) override {
        // make sure we have the file opened
        if (!ensureFile(new_file))
            return false;

        const std::map<llvm::Value *, LLVMDependenceGraph *> &CF =
                getConstructedFunctions();

        start();

        for (const auto &F : CF) {
            if (dump_func_only && !F.first->getName().equals(dump_func_only))
                continue;

            dumpSubgraph(F.second, F.first->getName().data());
        }

        end();

        return true;
    }

  private:
    void dumpSubgraph(LLVMDependenceGraph *graph, const char *name) {
        dumpSubgraphStart(graph, name);

        for (auto &B : graph->getBlocks()) {
            dumpBBlock(B.second);
        }

        for (auto &B : graph->getBlocks()) {
            dumpBBlockEdges(B.second);
        }

        dumpSubgraphEnd(graph);
    }
};

class LLVMDGDumpBlocks : public debug::DG2Dot<LLVMNode> {
  public:
    LLVMDGDumpBlocks(LLVMDependenceGraph *dg,
                     uint32_t opts = debug::PRINT_CFG | debug::PRINT_DD |
                                     debug::PRINT_CD,
                     const char *file = nullptr)
            : debug::DG2Dot<LLVMNode>(dg, opts, file) {}

    /* virtual
    std::ostream& printKey(std::ostream& os, llvm::Value *val)
    {
        return printLLVMVal(os, val);
    }
    */

    /* virtual */
    bool checkNode(std::ostream & /*os*/, LLVMNode * /*node*/) override {
        return false; // no error
    }

    bool dump(const char *new_file = nullptr,
              const char *dump_func_only = nullptr) override {
        // make sure we have the file opened
        if (!ensureFile(new_file))
            return false;

        const std::map<llvm::Value *, LLVMDependenceGraph *> &CF =
                getConstructedFunctions();

        start();

        for (const auto &F : CF) {
            // XXX: this is inefficient, we can get the dump_func_only function
            // from the module (F.getParent()->getModule()->getFunction(...)
            if (dump_func_only && !F.first->getName().equals(dump_func_only))
                continue;

            dumpSubgraph(F.second, F.first->getName().data());
        }

        end();

        return true;
    }

  private:
    void dumpSubgraph(LLVMDependenceGraph *graph, const char *name) {
        dumpSubgraphStart(graph, name);

        for (auto &B : graph->getBlocks()) {
            dumpBlock(B.second);
        }

        for (auto &B : graph->getBlocks()) {
            dumpBlockEdges(B.second);
        }

        dumpSubgraphEnd(graph, false);
    }

    void dumpBlock(LLVMBBlock *blk) {
        out << "NODE" << blk << " [label=\"";

        std::ostringstream ostr;
        llvm::raw_os_ostream ro(ostr);

        ro << *blk->getKey();
        ro.flush();
        std::string str = ostr.str();

        unsigned int i = 0;
        unsigned int len = 0;
        while (str[i] != 0) {
            if (len >= 40) {
                str[i] = '\n';
                len = 0;
            } else
                ++len;

            if (str[i] == '\n')
                len = 0;

            ++i;
        }

        unsigned int slice_id = blk->getSlice();
        if (slice_id != 0)
            out << "\\nslice: " << slice_id << "\\n";
        out << str << "\"";

        if (slice_id != 0)
            out << "style=filled fillcolor=greenyellow";

        out << "]\n";
    }

    void dumpBlockEdges(LLVMBBlock *blk) {
        for (const LLVMBBlock::BBlockEdge &edge : blk->successors()) {
            out << "NODE" << blk << " -> NODE" << edge.target
                << " [penwidth=2 label=\"" << static_cast<int>(edge.label)
                << "\"] \n";
        }

        for (const LLVMBBlock *pdf : blk->controlDependence()) {
            out << "NODE" << blk << " -> NODE" << pdf
                << " [color=blue constraint=false]\n";
        }
    }
};
} /* namespace debug */
} /* namespace dg */

#endif
