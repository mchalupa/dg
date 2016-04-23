#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <set>
#include <cassert>
#include <cstring>

#include "analysis/PointsTo/PSS.h"
#include "analysis/Offset.h"

#include "ADT/Queue.h"
#include "RDMap.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum RDNodeType {
        // for backward compatibility
        NONE = 0,
        // these are nodes that just represent memory allocation sites
        // we need to have them even in reaching definitions analysis,
        // so that we can use them as targets in DefSites
        ALLOC = 1,
        STORE,
        DYN_ALLOC,
        PHI,
        // return from the subprocedure
        RETURN,
        // call node
        CALL,
        // return from the call (in caller)
        CALL_RETURN,
        // dummy nodes
        NOOP
};

extern RDNode *UNKNOWN_MEMORY;

class RDNode {
    std::vector<RDNode *> successors;
    std::vector<RDNode *> predecessors;

    RDNodeType type;

    // marks for DFS/BFS
    unsigned int dfsid;

    // same data as in PSSNode
    const char *name;
    void *data;
    void *user_data;
public:
    RDNode(RDNodeType t = NONE)
        : type(t), dfsid(0), name(nullptr),
          data(nullptr), user_data(nullptr) {}

    // this is the gro of this node, so make it public
    DefSiteSetT defs;
    // this is a subset of defs that are strong update
    // on this node
    DefSiteSetT overwrites;

    RDMap def_map;

    RDNodeType getType() const { return type; }

    const char *getName() const { return name; }
    void setName(const char *n) { delete name; name = strdup(n); }

    const std::vector<RDNode *>& getSuccessors() const { return successors; }
    const std::vector<RDNode *>& getPredecessors() const { return predecessors; }
    size_t predecessorsNum() const { return predecessors.size(); }
    size_t successorsNum() const { return successors.size(); }

    DefSiteSetT& getDefines() { return defs; }
    const DefSiteSetT& getDefines() const { return defs; }

    void addSuccessor(RDNode *succ)
    {
        successors.push_back(succ);
        succ->predecessors.push_back(this);
    }

    // get successor when we know there's only one of them
    RDNode *getSingleSuccessor() const
    {
        assert(successors.size() == 1);
        return successors.front();
    }

    // get predecessor when we know there's only one of them
    RDNode *getSinglePredecessor() const
    {
        assert(predecessors.size() == 1);
        return predecessors.front();
    }

    bool defines(RDNode *target, const Offset& off = UNKNOWN_OFFSET) const
    {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        // (see old DefMap.h in llvm/)
        if (off.isUnknown()) {
            for (const DefSite& ds : defs)
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite& ds : defs)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    void addDef(const DefSite& ds, bool strong_update = false)
    {
        defs.insert(ds);
        def_map.update(ds, this);

        // XXX maybe we could do it by some flag in DefSite?
        // instead of strong new copy... but it should not
        // be big overhead this way... we'll see in the future
        if (strong_update)
            overwrites.insert(ds);
    }

    void addDef(RDNode *target,
                const Offset& off = UNKNOWN_OFFSET,
                const Offset& len = UNKNOWN_OFFSET,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    const RDMap& getReachingDefinitions() const { return def_map; }
    RDMap& getReachingDefinitions() { return def_map; }
    size_t getReachingDefinitions(RDNode *n, const Offset& off,
                                  const Offset& len, std::set<RDNode *>& ret)
    {
        return def_map.get(n, off, len, ret);
    }

    // getters & setters for analysis's data in the node
    template <typename T>
    T* getData() { return static_cast<T *>(data); }
    template <typename T>
    const T* getData() const { return static_cast<T *>(data); }

    template <typename T>
    void *setData(T *newdata)
    {
        void *old = data;
        data = static_cast<void *>(newdata);
        return old;
    }

    // getters & setters for user's data in the node
    template <typename T>
    T* getUserData() { return static_cast<T *>(user_data); }
    template <typename T>
    const T* getUserData() const { return static_cast<T *>(user_data); }

    template <typename T>
    void *setUserData(T *newdata)
    {
        void *old = user_data;
        user_data = static_cast<void *>(newdata);
        return old;
    }


    friend class ReachingDefinitionsAnalysis;
};

class ReachingDefinitionsAnalysis
{
    RDNode *root;
    unsigned int dfsnum;

public:
    ReachingDefinitionsAnalysis(RDNode *r): root(r), dfsnum(0)
    {
        assert(r && "Root cannot be null");
    }

    void getNodes(std::set<RDNode *>& cont)
    {
        assert(root && "Do not have root");

        ++dfsnum;

        ADT::QueueLIFO<RDNode *> lifo;
        lifo.push(root);
        root->dfsid = dfsnum;

        while (!lifo.empty()) {
            RDNode *cur = lifo.pop();
            cont.insert(cur);

            for (RDNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    lifo.push(succ);
                }
            }
        }
    }

    RDNode *getRoot() const { return root; }
    void setRoot(RDNode *r) { root = r; }

    bool processNode(RDNode *n);

    void run()
    {
        assert(root && "Do not have root");

        std::set<RDNode *> nodes;
        getNodes(nodes);

        // do fixpoint
        bool changed;
        do {
            changed = false;

            for (RDNode *cur : nodes)
                changed |= processNode(cur);

        } while (changed);
    }
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
