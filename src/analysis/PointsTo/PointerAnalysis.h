#ifndef _DG_POINTER_ANALYSIS_H_
#define _DG_POINTER_ANALYSIS_H_

#include <cassert>
#include <vector>

#include "Pointer.h"
#include "PointerSubgraph.h"
#include "ADT/Queue.h"

#include "analysis/SCC.h"

namespace dg {
namespace analysis {
namespace pta {

// special nodes
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;

class PointerAnalysis
{
    // the pointer state subgraph
    PointerSubgraph *PS;

    // strongly connected components of the PointerSubgraph
    std::vector<std::vector<PSNode *> > SCCs;

    // Maximal offset that we want to keep
    // within a pointer.
    // Default is unconstrained (UNKNOWN_OFFSET)
    uint64_t max_offset;

    // Flow sensitive flag (contol loop optimization execution)
    bool preprocess_geps;

protected:
    // a set of changed nodes that are going to be
    // processed by the analysis
    std::vector<PSNode *> to_process;
    std::vector<PSNode *> changed;

    // protected constructor for child classes
    PointerAnalysis() : PS(nullptr), max_offset(UNKNOWN_OFFSET),
                         preprocess_geps(true) {}

public:
    PointerAnalysis(PointerSubgraph *ps,
                    uint64_t max_off = UNKNOWN_OFFSET,
                    bool prepro_geps = true)
    : PS(ps), max_offset(max_off), preprocess_geps(prepro_geps)
    {
        assert(PS && "Need valid PointerSubgraph object");

        // compute the strongly connected components
        if (prepro_geps)
        {
            SCC<PSNode> scc_comp;
            SCCs = std::move(scc_comp.compute(PS->getRoot()));
        }
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
        changed.push_back(n);
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

    void preprocessGEPs()
    {
        // if a node is in a loop (a scc that has more than one node),
        // then every GEP that is also stored to the same memory afterwards
        // in the loop will end up with UNKNOWN_OFFSET after some
        // number of iterations, so we can do that right now
        // and save iterations
        for (const auto& scc : SCCs) {
            if (scc.size() > 1) {
                for (PSNode *n : scc) {
                    if (n->getType() == GEP)
                        n->setOffset(UNKNOWN_OFFSET);
                }
            }
        }
    }

    void run()
    {
        PSNode *root = PS->getRoot();
        assert(root && "Do not have root of PS");

        // do some optimizations
        if (preprocess_geps)
            preprocessGEPs();

        // rely on C++11 move semantics
        to_process = PS->getNodes(root);

        // do fixpoint
        do {
            unsigned last_processed_num = to_process.size();
            changed.clear();

            for (PSNode *cur : to_process) {
                beforeProcessed(cur);

                if (processNode(cur))
                    enqueue(cur);

                afterProcessed(cur);
            }

            if (!changed.empty()) {
                to_process.clear();
                to_process = PS->getNodes(nullptr /* starting node */,
                                          &changed /* starting set */,
                                          last_processed_num /* expected num */);

                // since changed was not empty,
                // the to_process must not be empty too
                assert(!to_process.empty());
            }
        } while (!changed.empty());
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
    bool processNode(PSNode *);
    bool processLoad(PSNode *node);
    bool processMemcpy(PSNode *node);
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
