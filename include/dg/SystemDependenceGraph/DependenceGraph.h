#ifndef DG_DEPENDENCE_GRAPH_H_
#define DG_DEPENDENCE_GRAPH_H_

#include <cassert>
#include <string>
#include <vector>

#include "DGBBlock.h"
#include "DGNode.h"
#include "DGNodeCall.h"
#include "DGParameters.h"

namespace dg {
namespace sdg {

class SystemDependenceGraph;
class DGElement;

///
// Dependence graph for one procedure in an system dependence graph
// (in papers refered to as Program Dependence Graph)
class DependenceGraph {
    friend class SystemDependenceGraph;
    friend unsigned DGElement::getNewID(DependenceGraph &g);

    unsigned _id{0};
    unsigned _lastNodeID{0};

    // SDG to which this dependence graph belongs
    SystemDependenceGraph &_sdg;
    // parameters associated to this graph
    DGFormalParameters _parameters;

    using NodesContainerTy = std::vector<std::unique_ptr<DGNode>>;
    using BBlocksContainerTy = std::vector<std::unique_ptr<DGBBlock>>;
    using CallersContainerTy = std::set<DGNodeCall *>;

    NodesContainerTy _nodes;
    BBlocksContainerTy _bblocks;
    // call nodes that call this function
    CallersContainerTy _callers;

    // only SystemDependenceGraph can create new DependenceGraph's
    DependenceGraph(unsigned id, SystemDependenceGraph &g)
            : _id(id), _sdg(g), _parameters(*this) {
        assert(id > 0);
    }

    std::string _name;

    // wrapper around block iterator that unwraps the unique_ptr
    struct bblocks_iterator : public decltype(_bblocks.begin()) {
        using OrigItType = decltype(_bblocks.begin());

        bblocks_iterator() = default;
        bblocks_iterator(const bblocks_iterator &I) = default;
        bblocks_iterator(const OrigItType &I) : OrigItType(I) {}

        DGBBlock *operator*() { return OrigItType::operator*().get(); }
        // DependenceGraph* operator->() { return OrigItType::operator*().get();
        // }
    };

    class bblocks_range {
        friend class DependenceGraph;

        BBlocksContainerTy &_C;

        bblocks_range(BBlocksContainerTy &C) : _C(C) {}

      public:
        bblocks_iterator begin() { return bblocks_iterator(_C.begin()); }
        bblocks_iterator end() { return bblocks_iterator(_C.end()); }
    };

    // wrapper around nodes iterator that unwraps the unique_ptr
    struct nodes_iterator : public decltype(_nodes.begin()) {
        using OrigItType = decltype(_nodes.begin());

        nodes_iterator() = default;
        nodes_iterator(const nodes_iterator &I) = default;
        nodes_iterator(const OrigItType &I) : OrigItType(I) {}

        DGNode *operator*() { return OrigItType::operator*().get(); }
        // DependenceGraph* operator->() { return OrigItType::operator*().get();
        // }
    };

    class nodes_range {
        friend class DependenceGraph;

        NodesContainerTy &_C;

        nodes_range(NodesContainerTy &C) : _C(C) {}

      public:
        nodes_iterator begin() { return nodes_iterator(_C.begin()); }
        nodes_iterator end() { return nodes_iterator(_C.end()); }
    };

    unsigned getNextNodeID() {
        // we could use _nodes.size(), but this is more error-prone
        // as this function could not increate _nodes.size()
        return ++_lastNodeID;
    }

  public:
    unsigned getID() const { return _id; }
    SystemDependenceGraph &getSDG() { return _sdg; }
    const SystemDependenceGraph &getSDG() const { return _sdg; }

    void setName(const std::string &nm) { _name = nm; }
    const std::string &getName() const { return _name; }

    // FIXME: rename to blocks() and nodes()
    bblocks_range getBBlocks() { return {_bblocks}; }
    nodes_range getNodes() { return {_nodes}; }

    // we do not have any total order on nodes or blocks in SDG,
    // but sometimes we need to get "some" node/block, so add
    // a getter for the first element in containers
    DGBBlock *getEntryBBlock() {
        return _bblocks.empty() ? nullptr : _bblocks.begin()->get();
    }

    const DGBBlock *getEntryBBlock() const {
        return _bblocks.empty() ? nullptr : _bblocks.begin()->get();
    }

    DGNode *getFirstNode() {
        return _nodes.empty() ? nullptr : _nodes.begin()->get();
    }

    const DGNode *getFirstNode() const {
        return _nodes.empty() ? nullptr : _nodes.begin()->get();
    }

    DGNodeInstruction &createInstruction() {
        auto *nd = new DGNodeInstruction(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGNodeCall &createCall() {
        auto *nd = new DGNodeCall(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGNodeArtificial &createArtificial() {
        auto *nd = new DGNodeArtificial(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGBBlock &createBBlock() {
        _bblocks.emplace_back(new DGBBlock(*this));
        return *_bblocks.back().get();
    }

    void addCaller(DGNodeCall *n) { _callers.insert(n); }

    const CallersContainerTy &getCallers() const { return _callers; }

    DGFormalParameters &getParameters() { return _parameters; }
    const DGFormalParameters &getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif // DG_DEPENDENCE_GRAPH_H_
