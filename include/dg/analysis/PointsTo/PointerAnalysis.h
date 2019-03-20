#ifndef _DG_POINTER_ANALYSIS_H_
#define _DG_POINTER_ANALYSIS_H_

#include <cassert>
#include <vector>

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/MemoryObject.h"
#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerAnalysisOptions.h"
#include "dg/ADT/Queue.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {
namespace pta {

// special nodes and pointers to them
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;
extern const Pointer NullPointer;
extern const Pointer UnknownPointer;

class PointerAnalysis
{
    void initPointerAnalysis() {
        assert(PS && "Need PointerGraph object");
    }

protected:
    // a set of changed nodes that are going to be
    // processed by the analysis
    std::vector<PSNode *> to_process;
    std::vector<PSNode *> changed;

    // the pointer state subgraph
    PointerGraph *PS{nullptr};

    const PointerAnalysisOptions options{};

public:

    PointerAnalysis(PointerGraph *ps,
                    const PointerAnalysisOptions& opts)
    : PS(ps), options(opts) {
        initPointerAnalysis();
    }

    // default options
    PointerAnalysis(PointerGraph *ps) : PointerAnalysis(ps, {}) {}

    virtual ~PointerAnalysis() {}

    // takes a PSNode 'where' and 'what' and reference to a vector
    // and fills into the vector the objects that are relevant
    // for the PSNode 'what' (valid memory states for of this PSNode)
    // on location 'where' in PointerGraph
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

    PointerGraph *getPS() const { return PS; }


    virtual void enqueue(PSNode *n)
    {
        changed.push_back(n);
    }

    virtual void preprocess() { }

    void initialize_queue() {
        assert(to_process.empty());

        PSNode *root = PS->getRoot();
        assert(root && "Do not have root of PS");
        // rely on C++11 move semantics
        to_process = PS->getNodes(root);
    }

    void queue_globals() {
        assert(to_process.empty());
        for (auto& it : PS->getGlobals()) {
            to_process.push_back(it.get());
        }
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
                                      true /* interprocedural */,
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
        DBG_SECTION_BEGIN(pta, "Running pointer analysis");

        preprocess();

        // check that the current state of pointer analysis makes sense
        sanityCheck();

        // process global nodes, these must reach fixpoint after one iteration
        DBG(pta, "Processing global nodes");
        queue_globals();
        iteration();
        assert((to_process.clear(), changed.clear(), queue_globals(), !iteration()) && "Globals did not reach fixpoint");
        to_process.clear();
        changed.clear();

        initialize_queue();

#if DEBUG_ENABLED
        int n = 0;
#endif
        // do fixpoint
        do {
#if DEBUG_ENABLED
            if (n % 100 == 0) {
                DBG(pta, "Iteration " << n << ", queue size " << to_process.size());
            }
            ++n;
#endif

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

        DBG_SECTION_END(pta, "Running pointer analysis done");
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

    // adjust the PointerGraph on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSNode * /*where*/, PSNode * /*what*/)
    {
        return false;
    }

    virtual bool handleFork(PSNode *)
    {
        return false;
    }

    virtual bool handleJoin(PSNode *)
    {
        return false;
    }

private:

    // check the sanity of results of pointer analysis
    void sanityCheck();

    bool processNode(PSNode *);
    bool processLoad(PSNode *node);
    bool processGep(PSNode *node);
    bool processMemcpy(PSNode *node);
    bool processMemcpy(std::vector<MemoryObject *>& srcObjects,
                       std::vector<MemoryObject *>& destObjects,
                       const Pointer& sptr, const Pointer& dptr,
                       Offset len);

};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
