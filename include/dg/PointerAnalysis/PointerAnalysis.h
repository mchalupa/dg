#ifndef DG_POINTER_ANALYSIS_H_
#define DG_POINTER_ANALYSIS_H_

#include <cassert>
#include <utility>
#include <vector>

#include "dg/ADT/Queue.h"
#include "dg/PointerAnalysis/MemoryObject.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointerAnalysisOptions.h"
#include "dg/PointerAnalysis/PointerGraph.h"

namespace dg {
namespace pta {

class PointerAnalysis {
    static void initPointerAnalysis() {}

  protected:
    // a set of changed nodes that are going to be
    // processed by the analysis
    std::vector<PSNode *> to_process;
    std::vector<PSNode *> changed;

    // the pointer state subgraph
    PointerGraph *PG{nullptr};

    const PointerAnalysisOptions options{};

  public:
    PointerAnalysis(PointerGraph *ps, PointerAnalysisOptions opts)
            : PG(ps), options(std::move(opts)) {
        initPointerAnalysis();
    }

    // default options
    PointerAnalysis(PointerGraph *ps) : PointerAnalysis(ps, {}) {}

    virtual ~PointerAnalysis() = default;

    // takes a PSNode 'where' and 'what' and reference to a vector
    // and fills into the vector the objects that are relevant
    // for the PSNode 'what' (valid memory states for of this PSNode)
    // on location 'where' in PointerGraph
    virtual void getMemoryObjects(PSNode *where, const Pointer &pointer,
                                  std::vector<MemoryObject *> &objects) = 0;

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
    virtual bool beforeProcessed(PSNode * /*unused*/) { return false; }

    virtual bool afterProcessed(PSNode * /*unused*/) { return false; }

    PointerGraph *getPG() { return PG; }
    const PointerGraph *getPG() const { return PG; }

    virtual void enqueue(PSNode *n) { changed.push_back(n); }

    virtual void preprocess() {}

    void initialize_queue() {
        assert(to_process.empty());

        PSNode *root = PG->getEntry()->getRoot();
        assert(root && "Do not have root of PG");
        // rely on C++11 move semantics
        to_process = PG->getNodes(root);
    }

    void queue_globals() {
        assert(to_process.empty());
        for (auto *g : PG->getGlobals()) {
            to_process.push_back(g);
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
            to_process = PG->getNodes(changed /* starting set */,
                                      true /* interprocedural */,
                                      last_processed_num /* expected num */);

            // since changed was not empty,
            // the to_process must not be empty too
            assert(!to_process.empty());
            assert(to_process.size() >= changed.size());
            changed.clear();
        }
    }

    bool run();

    // generic error
    // @msg - message for the user
    // XXX: maybe create some enum that will represent the error
    virtual bool error(PSNode * /*at*/, const char * /*msg*/) {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        return false;
    }

    // handle specific situation (error) in the analysis
    // @return whether the function changed the some points-to set
    //  (e. g. added pointer to unknown memory)
    virtual bool errorEmptyPointsTo(PSNode * /*from*/, PSNode * /*to*/) {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        return false;
    }

    // adjust the PointerGraph on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSNode * /*where*/, PSNode * /*what*/) {
        return false;
    }

    // adjust the PointerGraph on when a new function that can be
    // spawned by fork is discovered
    // @ fork is the callsite
    // @ called is the function that is being called
    virtual bool handleFork(PSNode * /* fork */, PSNode * /* called */) {
        return false;
    }

    // handle join of threads
    // FIXME: this should be done in the generic pointer analysis,
    // we do not need to pass this to the LLVM part...
    virtual bool handleJoin(PSNode * /*unused*/) { return false; }

  private:
    // check the sanity of results of pointer analysis
    void sanityCheck();

    bool processNode(PSNode * /*node*/);
    bool processLoad(PSNode *node);
    bool processGep(PSNode *node);
    bool processMemcpy(PSNode *node);
    bool processMemcpy(std::vector<MemoryObject *> &srcObjects,
                       std::vector<MemoryObject *> &destObjects,
                       const Pointer &sptr, const Pointer &dptr, Offset len);
};

} // namespace pta
} // namespace dg

#endif
