#ifndef _LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_
#define _LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_


// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/FormattedStream.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Assembly/AssemblyAnnotationWriter.h>
 #include <llvm/Analysis/Verifier.h>
#else // >= 3.5
 #include <llvm/IR/AssemblyAnnotationWriter.h>
 #include <llvm/IR/Verifier.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/analysis/PointsTo/PointerAnalysis.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

namespace dg {
namespace debug {

class LLVMDGAssemblyAnnotationWriter : public llvm::AssemblyAnnotationWriter
{
    using LLVMReachingDefinitions = dg::analysis::rd::LLVMReachingDefinitions;
public:
    enum AnnotationOptsT {
        // data dependencies
        ANNOTATE_DD                 = 1 << 0,
        // forward data dependencies
        ANNOTATE_FORWARD_DD         = 1 << 1,
        // control dependencies
        ANNOTATE_CD                 = 1 << 2,
        // points-to information
        ANNOTATE_PTR                = 1 << 3,
        // reaching definitions
        ANNOTATE_RD                 = 1 << 4,
        // post-dominators
        ANNOTATE_POSTDOM            = 1 << 5,
        // comment out nodes that will be sliced
        ANNOTATE_SLICE              = 1 << 6,
    };

private:

    AnnotationOptsT opts;
    LLVMPointerAnalysis *PTA;
    LLVMReachingDefinitions *RD;
    const std::set<LLVMNode *> *criteria;
    std::string module_comment{};

    void printValue(const llvm::Value *val,
                    llvm::formatted_raw_ostream& os,
                    bool nl = false)
    {
        if (val->hasName())
            os << val->getName().data();
        else
            os << *val;

        if (nl)
            os << "\n";
    }

    void printPointer(const analysis::pta::Pointer& ptr,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = "PTR: ", bool nl = true)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (!ptr.isUnknown()) {
            if (ptr.isNull())
                os << "null";
            else if (ptr.isInvalidated())
                os << "invalidated";
            else {
                const llvm::Value *val
                    = ptr.target->getUserData<llvm::Value>();
                printValue(val, os);
            }

            os << " + ";
            if (ptr.offset.isUnknown())
                os << "UNKNOWN";
            else
                os << *ptr.offset;
        } else
            os << "unknown";

        if (nl)
            os << "\n";
    }

    void printDefSite(const analysis::rd::DefSite& ds,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = nullptr, bool nl = false)
    {
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
                os << " bytes |UNKNOWN";
            else
                os << " bytes |" << *ds.offset;

            if (ds.len.isUnknown())
                os << " - UNKNOWN|";
            else
                os << " - " <<  *ds.offset + *ds.len- 1 << "|";
        } else
            os << "target is null!";

        if (nl)
            os << "\n";

    }

    void emitNodeAnnotations(LLVMNode *node, llvm::formatted_raw_ostream& os)
    {
        if (opts & ANNOTATE_RD) {
            assert(RD && "No reaching definitions analysis");
            analysis::rd::RDNode *rd = RD->getMapping(node->getKey());
            if (!rd) {
                os << "  ; RD: no mapping\n";
            } else {
                auto& defs = rd->getReachingDefinitions();
                for (auto& it : defs) {
                    for (auto& nd : it.second) {
                        printDefSite(it.first, os, "RD: ");
                        os << " @ ";
                        if (nd->isUnknown())
                            os << " UNKNOWN\n";
                        else
                            printValue(nd->getUserData<llvm::Value>(), os, true);
                    }
                }
            }

            LLVMDGParameters *params = node->getParameters();
            // don't dump params when we use new analyses (RD is not null)
            // because there we don't add definitions with new analyses
            if (params && !RD) {
                for (auto& it : *params) {
                    os << "  ; PARAMS: in " << it.second.in
                       << ", out " << it.second.out << "\n";

                    // dump edges for parameters
                    os <<"  ; in edges\n";
                    emitNodeAnnotations(it.second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it.second.out, os);
                    os << "\n";
                }

                for (auto it = params->global_begin(), et = params->global_end();
                     it != et; ++it) {
                    os << "  ; PARAM GL: in " << it->second.in
                       << ", out " << it->second.out << "\n";

                    // dump edges for parameters
                    os << "  ; in edges\n";
                    emitNodeAnnotations(it->second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it->second.out, os);
                    os << "\n";
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
            for (auto I = node->data_begin(), E = node->data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; fDD: " << *d << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_CD) {
            for (auto I = node->rev_control_begin(), E = node->rev_control_end();
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
                    analysis::pta::PSNode *ps = PTA->getPointsTo(node->getKey());
                    if (ps) {
                        for (const analysis::pta::Pointer& ptr : ps->pointsTo)
                            printPointer(ptr, os);
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
    LLVMDGAssemblyAnnotationWriter(AnnotationOptsT o = ANNOTATE_SLICE,
                                   LLVMPointerAnalysis *pta = nullptr,
                                   LLVMReachingDefinitions *rd = nullptr,
                                   const std::set<LLVMNode *>* criteria = nullptr)
        : opts(o), PTA(pta), RD(rd), criteria(criteria)
    {
        assert(!(opts & ANNOTATE_PTR) || PTA);
        assert(!(opts & ANNOTATE_RD) || RD);
    }

    void emitModuleComment(const std::string& comment) {
        module_comment = comment;
    }

    void emitModuleComment(std::string&& comment) {
        module_comment = std::move(comment);
    }

    void emitFunctionAnnot (const llvm::Function *,
                            llvm::formatted_raw_ostream &os) override
    {
        // dump the slicer's setting to the file
        // for easier comprehension
        static bool didit = false;
        if (!didit) {
            didit = true;
            os << module_comment;
        }
    }

    void emitInstructionAnnot(const llvm::Instruction *I,
                              llvm::formatted_raw_ostream& os) override
    {
        if (opts == 0)
            return;

        LLVMNode *node = nullptr;
        for (auto& it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            node = sub->getNode(const_cast<llvm::Instruction *>(I));
            if (node)
                break;
        }

        if (!node)
            return;

        emitNodeAnnotations(node, os);
    }

    void emitBasicBlockStartAnnot(const llvm::BasicBlock *B,
                                  llvm::formatted_raw_ostream& os) override
    {
        if (opts == 0)
            return;

        for (auto& it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            auto& cb = sub->getBlocks();
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
operator |(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT a,
           dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {

  using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
  using T = std::underlying_type<AnT>::type;
  return static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
}

inline dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT
operator |=(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT& a,
           dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {

  using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
  using T = std::underlying_type<AnT>::type;
  a = static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
  return a;
}

#endif // _LLVM_DG_ANNOTATION_WRITER_H_

