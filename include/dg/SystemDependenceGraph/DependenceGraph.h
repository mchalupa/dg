#ifndef DG_DEPENDENCE_GRAPH_H_
#define DG_DEPENDENCE_GRAPH_H_

#include <cassert>
#include <vector>

#include "DGNode.h"
#include "DGNodeCall.h"
#include "DGBBlock.h"
#include "DGParameters.h"

namespace dg {
namespace sdg {

class SystemDependenceGraph;

///
// Dependence graph for one procedure in an system dependence graph
// (in papers refered to as Program Dependence Graph)
class DependenceGraph {
    friend class SystemDependenceGraph;
    friend unsigned DGNode::getNewID(DependenceGraph& g);

    unsigned _id{0};
    unsigned _lastNodeID{0};

    // SDG to which this dependence graph belongs
    SystemDependenceGraph& _sdg;
    // parameters associated to this graph
    DGFormalParameters _parameters;

    using BBlocksContainerTy = std::vector<std::unique_ptr<DGBBlock>>;

    std::vector<std::unique_ptr<DGNode>> _nodes;
    BBlocksContainerTy _bblocks;
    // only SystemDependenceGraph can create new DependenceGraph's
    DependenceGraph(unsigned id, SystemDependenceGraph& g)
    : _id(id), _sdg(g), _parameters(*this) { assert(id > 0); }

    std::string _name;

    // wrapper around graphs iterator that unwraps the unique_ptr
    struct bblocks_iterator : public decltype(_bblocks.begin()) {
        using OrigItType = decltype(_bblocks.begin());

        bblocks_iterator() = default;
        bblocks_iterator(const bblocks_iterator& I) = default;
        bblocks_iterator(const OrigItType& I) : OrigItType(I) {}

        DGBBlock* operator*() { return OrigItType::operator*().get(); }
        //DependenceGraph* operator->() { return OrigItType::operator*().get(); }
    };

    class bblocks_range {
        friend class DependenceGraph;

        BBlocksContainerTy& _C;

        bblocks_range(BBlocksContainerTy& C) : _C(C) {}
    public:
        bblocks_iterator begin() { return bblocks_iterator(_C.begin()); }
        bblocks_iterator end() { return bblocks_iterator(_C.end()); }
    };

    unsigned getNextNodeID() {
        // we could use _nodes.size(), but this is more error-prone
        // as this function could not increate _nodes.size()
        return ++_lastNodeID;
    }

public:

    unsigned getID() const { return _id; }
    SystemDependenceGraph& getSDG() { return _sdg; }
    const SystemDependenceGraph& getSDG() const { return _sdg; }

    void setName(const std::string& nm) { _name = nm; }
    const std::string& getName() const { return _name; }

    bblocks_range getBBlocks() { return bblocks_range(_bblocks); }

    DGNodeInstruction& createInstruction() {
        auto *nd = new DGNodeInstruction(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGNodeCall& createCall() {
        auto *nd = new DGNodeCall(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGNodeArtificial& createArtificial() {
        auto *nd = new DGNodeArtificial(*this);
        _nodes.emplace_back(nd);
        return *nd;
    }

    DGBBlock& createBBlock() {
        _bblocks.emplace_back(new DGBBlock(_bblocks.size() + 1, this));
        return *_bblocks.back().get();
    }

    DGFormalParameters& getParameters() { return _parameters; }
    const DGFormalParameters& getParameters() const { return _parameters; }
};

} // namespace sdg
} // namespace dg

#endif // DG_DEPENDENCE_GRAPH_H_
