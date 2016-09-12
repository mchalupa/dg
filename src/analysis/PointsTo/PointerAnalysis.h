#ifndef _DG_POINTER_ANALYSIS_H_
#define _DG_POINTER_ANALYSIS_H_

#include <cassert>
#include <vector>

#include "Pointer.h"
#include "PointerSubgraph.h"
#include "ADT/Queue.h"

namespace dg {
namespace analysis {
namespace pta {

// special nodes
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;

class PointerAnalysis
{
    unsigned int dfsnum;

    // the pointer state subgraph
    PointerSubgraph *PS;

    // Maximal offset that we want to keep
    // within a pointer.
    // Default is unconstrained (UNKNOWN_OFFSET)
    uint64_t max_offset;

protected:
    // queue used to reach the fixpoint
    ADT::QueueFIFO<PSNode *> queue;

    // protected constructor for child classes
    PointerAnalysis() : dfsnum(0), PS(nullptr) {}

public:
    PointerAnalysis(PointerSubgraph *ps,
                    uint64_t max_off = UNKNOWN_OFFSET)
    : dfsnum(0), PS(ps), max_offset(max_off)
    {
        assert(PS && "Need valid PointerSubgraph object");
    }

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

    virtual void enqueue(PSNode *n)
    {
        // default behaviour is to queue all reachable nodes
        PS->getNodes(queue, n);
    }

    /* hooks for analysis - optional */
    virtual void beforeProcessed(PSNode *n)
    {
        (void) n;
    }

    virtual void afterProcessed(PSNode *n)
    {
        (void) n;
    }

    PointerSubgraph *getPS() const { return PS; }
    size_t pendingInQueue() const { return queue.size(); }

    void run()
    {
        PSNode *root = PS->getRoot();
        assert(root && "Do not have root");

        // initialize the queue
        // FIXME let the user do that
        queue.push(root);
        PS->getNodes(queue);

        while (!queue.empty()) {
            PSNode *cur = queue.pop();
            beforeProcessed(cur);

            if (processNode(cur))
                enqueue(cur);

            afterProcessed(cur);
        }

        // FIXME: There's a bug in flow-sensitive that it does
        // not reach fixpoint in the loop above, because it reads
        // from values that has not been processed yet (thus it has
        // empty points-to set) - nothing is changed, so it seems
        // that we reached fixpoint, but we didn't and we fail
        // the assert below. This is temporary workaround -
        // just make another iteration. Proper fix would be to
        // fix queuing the nodes, but that will be more difficult
        /* LET THE BUG APPEAR
        queue.push(root);
        PS->getNodes(queue);

        while (!queue.empty()) {
            PSNode *cur = queue.pop();
            beforeProcessed(cur);

            if (processNode(cur))
                enqueue(cur);

            afterProcessed(cur);
        }
        */

#ifdef DEBUG_ENABLED
        // NOTE: This works as assertion,
        // we'd like to be sure that we have reached the fixpoint,
        // so we'll do one more iteration and check it

        /*
        queue.push(root);
        PS->getNodes(queue);

        bool changed = false;
        while (!queue.empty()) {
            PSNode *cur = queue.pop();

            beforeProcessed(cur);

            changed = processNode(cur);
            assert(!changed && "BUG: Did not reach fixpoint");

            afterProcessed(cur);
        }
        */
#endif // DEBUG_ENABLED
    }

    // generic error
    // @msg - message for the user
    // FIXME: maybe create some enum that will represent the error
    virtual bool error(PSNode *at, const char *msg)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) at;
        (void) msg;
        return false;
    }

    // handle specific situation (error) in the analysis
    // @return whether the function changed the some points-to set
    //  (e. g. added pointer to unknown memory)
    virtual bool errorEmptyPointsTo(PSNode *from, PSNode *to)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) from;
        (void) to;
        return false;
    }

    // adjust the PointerSubgraph on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSNode *where, PSNode *what)
    {
        (void) where;
        (void) what;
        return false;
    }

private:
    bool processNode(PSNode *);
    bool processLoad(PSNode *node);
    bool processMemcpy(PSNode *node);
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
