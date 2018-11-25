#ifndef _DG_POINTER_ANALYSIS_H_
#define _DG_POINTER_ANALYSIS_H_

#include <cassert>
#include <vector>

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/MemoryObject.h"
#include "dg/analysis/PointsTo/PointerSubgraph.h"
#include "dg/analysis/PointsTo/PointerAnalysisOptions.h"
#include "dg/ADT/Queue.h"
#include "dg/analysis/SCC.h"

namespace dg {
namespace analysis {
namespace pta {

// special nodes
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;

class PointerAnalysis
{
    // the pointer state subgraph
    PointerSubgraph *PS{nullptr};
    const PointerAnalysisOptions options{};

    // strongly connected components of the PointerSubgraph
    std::vector<std::vector<PSNode *> > SCCs;
    unsigned sccs_index{0};

    void initPointerAnalysis() {
        assert(PS && "Need PointerSubgraph object");

        // compute the strongly connected components
        SCC<PSNode> scc_comp;
        SCCs = std::move(scc_comp.compute(PS->getRoot()));
        sccs_index = scc_comp.getIndex();
    }

protected:
    // a set of changed nodes that are going to be
    // processed by the analysis
    std::vector<PSNode *> to_process;
    std::vector<PSNode *> changed;

public:

    PointerAnalysis(PointerSubgraph *ps,
                    const PointerAnalysisOptions& opts)
    : PS(ps), options(opts) {
        initPointerAnalysis();
    }

    // default options
    PointerAnalysis(PointerSubgraph *ps) : PointerAnalysis(ps, {}) {}

    virtual ~PointerAnalysis() {}

    // takes a PSNode 'where' and 'what' and reference to a vector
    // and fills into the vector the objects that are relevant
    // for the PSNode 'what' (valid memory states for of this PSNode)
    // on location 'where' in PointerSubgraph
    virtual void getMemoryObjects(PSNode *where, const Pointer& pointer,
                                  std::vector<MemoryObject *>& objects) = 0;

    /*
    virtual bool addEdge(MemoryObject *from, MemoryObject *to,
                         Offset off1 = 0, Offset off2 = 0)
    {
        return false;
    }
    */

    /* hooks for analysis - optional. The analysis may do everything
     * in getMemoryObjects, but spliting it into before-get-after sequence
     * is more readable */
    virtual bool beforeProcessed(PSNode *) {
        return false;
    }

    virtual bool afterProcessed(PSNode *) {
        return false;
    }

    PointerSubgraph *getPS() const { return PS; }

    const std::vector<std::vector<PSNode *> > &getSCCs() const { return SCCs; }

    virtual void enqueue(PSNode *n)
    {
        changed.push_back(n);
    }

    void preprocess() {
        // do some optimizations
        if (options.preprocessGeps)
            preprocessGEPs();
    }

    void initialize_queue() {
        assert(to_process.empty());

        PSNode *root = PS->getRoot();
        assert(root && "Do not have root of PS");
        // rely on C++11 move semantics
        to_process = PS->getNodes(root);
    }


    bool iteration() {
        assert(changed.empty());

        for (PSNode *cur : to_process) {
            bool enq = false;
            enq |= beforeProcessed(cur);
            enq |= processNode(cur);
            enq |= afterProcessed(cur);

            if (enq)
                enqueue(cur);
        }

        return !changed.empty();
    }

    void queue_changed() {
        unsigned last_processed_num = to_process.size();
        to_process.clear();

        if (!changed.empty()) {
            // DONT std::move - it prevents compiler from copy ellision
            to_process = PS->getNodes(changed /* starting set */,
                                      last_processed_num /* expected num */);

            // since changed was not empty,
            // the to_process must not be empty too
            assert(!to_process.empty());
            assert(to_process.size() >= changed.size());
            changed.clear();
        }
    }

    void run()
    {
        // do preprocessing and queue the nodes
        preprocess();
        initialize_queue();

        // check that the current state of pointer analysis makes sense
        sanityCheck();

        // do fixpoint
        do {
            iteration();
            queue_changed();
        } while (!to_process.empty());

        assert(to_process.empty());
        assert(changed.empty());

        // NOTE: With flow-insensitive analysis, it may happen that
        // we have not reached the fixpoint here. This is beacuse
        // we queue only reachable nodes from the nodes that changed
        // something. So if in the rechable nodes something generates
        // new information, than this information could be added to some
        // node in a new iteration over all nodes. But this information
        // can never get to that node in runtime, since that node is
        // unreachable from the point where the information is
        // generated, so this is OK.

        sanityCheck();
    }

    // generic error
    // @msg - message for the user
    // XXX: maybe create some enum that will represent the error
    virtual bool error(PSNode * /*at*/, const char * /*msg*/)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        return false;
    }

    // handle specific situation (error) in the analysis
    // @return whether the function changed the some points-to set
    //  (e. g. added pointer to unknown memory)
    virtual bool errorEmptyPointsTo(PSNode * /*from*/, PSNode * /*to*/)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        return false;
    }

    // adjust the PointerSubgraph on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSNode * /*where*/, PSNode * /*what*/)
    {
        return false;
    }

private:

    // check the sanity of results of pointer analysis
    void sanityCheck();

    void preprocessGEPs()
    {
        // if a node is in a loop (a scc that has more than one node),
        // then every GEP that is also stored to the same memory afterwards
        // in the loop will end up with Offset::UNKNOWN after some
        // number of iterations, so we can do that right now
        // and save iterations
        for (const auto& scc : SCCs) {
            if (scc.size() > 1) {
                for (PSNode *n : scc) {
                    if (PSNodeGep *gep = PSNodeGep::get(n))
                        gep->setOffset(Offset::UNKNOWN);
                }
            }
        }
    }

    bool processNode(PSNode *);
    bool processLoad(PSNode *node);
    bool processGep(PSNode *node);
    bool processMemcpy(PSNode *node);
    bool processMemcpy(std::vector<MemoryObject *>& srcObjects,
                       std::vector<MemoryObject *>& destObjects,
                       const Pointer& sptr, const Pointer& dptr,
                       Offset len);

    void recomputeSCCs()
    {
        SCC<PSNode> scc_comp(sccs_index);
        SCCs = std::move(scc_comp.compute(PS->getRoot()));
        sccs_index = scc_comp.getIndex();
    }
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
