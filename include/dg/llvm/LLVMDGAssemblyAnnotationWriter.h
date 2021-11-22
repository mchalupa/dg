#ifndef LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_
#define LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_

#include <llvm/Support/FormattedStream.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/AssemblyAnnotationWriter.h>
#else // >= 3.5
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/Verifier.h>
#endif

#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

namespace dg {
namespace debug {

class LLVMDGAssemblyAnnotationWriter : public llvm::AssemblyAnnotationWriter {
    using LLVMDataDependenceAnalysis = dg::dda::LLVMDataDependenceAnalysis;

  public:
    enum AnnotationOptsT {
        // data dependencies
        ANNOTATE_DD = 1 << 0,
        // forward data dependencies
        ANNOTATE_FORWARD_DD = 1 << 1,
        // control dependencies
        ANNOTATE_CD = 1 << 2,
        // points-to information
        ANNOTATE_PTR = 1 << 3,
        // reaching definitions
        ANNOTATE_DEF = 1 << 4,
        // post-dominators
        ANNOTATE_POSTDOM = 1 << 5,
        // comment out nodes that will be sliced
        ANNOTATE_SLICE = 1 << 6,
        // annotate memory accesses (like ANNOTATE_PTR,
        // but with byte intervals)
        ANNOTATE_MEMORYACC = 1 << 7,
    };

  private:
    AnnotationOptsT opts;
    LLVMPointerAnalysis *PTA;
    LLVMDataDependenceAnalysis *DDA;
    const std::set<LLVMNode *> *criteria;
    std::string module_comment{};

    static void printValue(const llvm::Value *val,
                           llvm::formatted_raw_ostream &os, bool nl = false) {
        if (val->hasName())
            os << val->getName().data();
        else
            os << *val;

        if (nl)
            os << "\n";
    }

    static void printPointer(const LLVMPointer &ptr,
                             llvm::formatted_raw_ostream &os,
                             const char *prefix = "PTR: ", bool nl = true) {
        os << "  ; ";
        if (prefix)
            os << prefix;

        printValue(ptr.value, os);

        os << " + ";
        if (ptr.offset.isUnknown())
            os << "?";
        else
            os << *ptr.offset;

        if (nl)
            os << "\n";
    }

    static void printDefSite(const dda::DefSite &ds,
                             llvm::formatted_raw_ostream &os,
                             const char *prefix = nullptr, bool nl = false) {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (ds.target) {
            const llvm::Value *val = ds.target->getUserData<llvm::Value>();
            if (ds.target->isUnknown())
                os << "unknown";
            else
                printValue(val, os);

            if (ds.offset.isUnknown())
                os << " bytes |?";
            else
                os << " bytes |" << *ds.offset;

            if (ds.len.isUnknown())
                os << " - ?|";
            else
                os << " - " << *ds.offset + *ds.len - 1 << "|";
        } else
            os << "target is null!";

        if (nl)
            os << "\n";
    }

    static void printMemRegion(const LLVMMemoryRegion &R,
                               llvm::formatted_raw_ostream &os,
                               const char *prefix = nullptr, bool nl = false) {
        os << "  ; ";
        if (prefix)
            os << prefix;

        assert(R.pointer.value);
        printValue(R.pointer.value, os);

        if (R.pointer.offset.isUnknown())
            os << " bytes [?";
        else
            os << " bytes [" << *R.pointer.offset;

        if (R.len.isUnknown())
            os << " - ?]";
        else
            os << " - " << *R.pointer.offset + *R.len - 1 << "]";

        if (nl)
            os << "\n";
    }

    void emitNodeAnnotations(LLVMNode *node, llvm::formatted_raw_ostream &os) {
        using namespace llvm;

        if (opts & ANNOTATE_DEF) {
            assert(DDA && "No data dependence analysis");
            if (DDA->isUse(node->getValue())) {
                os << "  ; DEF: ";
                const auto &defs = DDA->getLLVMDefinitions(node->getValue());
                if (defs.empty()) {
                    os << "none (or global)\n";
                } else {
                    for (auto *def : defs) {
                        if (def->hasName())
                            os << def->getName();
                        else
                            os << *def;

                        os << "(" << def << ")\n";
                    }
                }
            }
        }

        if (opts & ANNOTATE_DD) {
            for (auto I = node->rev_data_begin(), E = node->rev_data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; DD: ";

                if (d->hasName())
                    os << d->getName();
                else
                    os << *d;

                os << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_FORWARD_DD) {
            for (auto I = node->data_begin(), E = node->data_end(); I != E;
                 ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; fDD: " << *d << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_CD) {
            for (auto I = node->rev_control_begin(),
                      E = node->rev_control_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; rCD: ";

                if (d->hasName())
                    os << d->getName() << "\n";
                else
                    os << *d << "\n";
            }
        }

        if (opts & ANNOTATE_PTR) {
            if (PTA) {
                llvm::Type *Ty = node->getKey()->getType();
                if (Ty->isPointerTy() || Ty->isIntegerTy()) {
                    const auto &ps = PTA->getLLVMPointsTo(node->getKey());
                    if (!ps.empty()) {
                        for (const auto &llvmptr : ps) {
                            printPointer(llvmptr, os);
                        }
                        if (ps.hasNull()) {
                            os << "  ; null\n";
                        }
                        if (ps.hasNullWithOffset()) {
                            os << "  ; null + ?\n";
                        }
                        if (ps.hasUnknown()) {
                            os << "  ; unknown\n";
                        }
                        if (ps.hasInvalidated()) {
                            os << "  ; invalidated\n";
                        }
                    }
                }
            }
        }

        if (PTA && (opts & ANNOTATE_MEMORYACC)) {
            if (auto *I = dyn_cast<Instruction>(node->getValue())) {
                if (I->mayReadOrWriteMemory()) {
                    auto regions = PTA->getAccessedMemory(I);
                    if (regions.first) {
                        os << "  ; unknown region\n";
                    }
                    for (const auto &mem : regions.second) {
                        printMemRegion(mem, os, nullptr, true);
                    }
                }
            }
        }

        if (opts & ANNOTATE_SLICE) {
            if (criteria && criteria->count(node) > 0)
                os << "  ; SLICING CRITERION\n";
            if (node->getSlice() == 0)
                os << "  ; x ";
        }
    }

  public:
    LLVMDGAssemblyAnnotationWriter(
            AnnotationOptsT o = ANNOTATE_SLICE,
            LLVMPointerAnalysis *pta = nullptr,
            LLVMDataDependenceAnalysis *dda = nullptr,
            const std::set<LLVMNode *> *criteria = nullptr)
            : opts(o), PTA(pta), DDA(dda), criteria(criteria) {
        assert(!(opts & ANNOTATE_PTR) || PTA);
        assert(!(opts & ANNOTATE_DEF) || DDA);
    }

    void emitModuleComment(const std::string &comment) {
        module_comment = comment;
    }

    void emitModuleComment(std::string &&comment) {
        module_comment = std::move(comment);
    }

    void emitFunctionAnnot(const llvm::Function * /*unused*/,
                           llvm::formatted_raw_ostream &os) override {
        // dump the slicer's setting to the file
        // for easier comprehension
        static bool didit = false;
        if (!didit) {
            didit = true;
            os << module_comment;
        }
    }

    void emitInstructionAnnot(const llvm::Instruction *I,
                              llvm::formatted_raw_ostream &os) override {
        if (opts == 0)
            return;

        LLVMNode *node = nullptr;
        for (const auto &it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            node = sub->getNode(const_cast<llvm::Instruction *>(I));
            if (node)
                break;
        }

        if (!node) {
            if (opts & ANNOTATE_SLICE)
                os << "  ; x ";
            return;
        }

        emitNodeAnnotations(node, os);
    }

    void emitBasicBlockStartAnnot(const llvm::BasicBlock *B,
                                  llvm::formatted_raw_ostream &os) override {
        if (opts == 0)
            return;

        for (const auto &it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            auto &cb = sub->getBlocks();
            auto I = cb.find(const_cast<llvm::BasicBlock *>(B));
            if (I != cb.end()) {
                LLVMBBlock *BB = I->second;
                if (opts & (ANNOTATE_POSTDOM | ANNOTATE_CD))
                    os << "  ; BB: " << BB << "\n";

                if (opts & ANNOTATE_POSTDOM) {
                    for (LLVMBBlock *p : BB->getPostDomFrontiers())
                        os << "  ; PDF: " << p << "\n";

                    LLVMBBlock *P = BB->getIPostDom();
                    if (P && P->getKey())
                        os << "  ; iPD: " << P << "\n";
                }

                if (opts & ANNOTATE_CD) {
                    for (LLVMBBlock *p : BB->controlDependence())
                        os << "  ; CD: " << p << "\n";
                }
            }
        }
    }
};

} // namespace debug
} // namespace dg

// allow combinations of annotation options
inline dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT
operator|(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT a,
          dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {
    using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
    using T = std::underlying_type<AnT>::type;
    return static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
}

inline dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT
operator|=(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT &a,
           dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {
    using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
    using T = std::underlying_type<AnT>::type;
    a = static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
    return a;
}

#endif // LLVM_DG_ANNOTATION_WRITER_H_
