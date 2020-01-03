#ifndef DG_DEPENDENCE_GRAPH_H_
#define DG_DEPENDENCE_GRAPH_H_

#include <cassert>
#include <vector>

#include "DGNode.h"
#include "DGBBlock.h"

namespace dg {
namespace sdg {

class SystemDependenceGraph;

///
// Dependence graph for one procedure in an system dependence graph
// (in papers refered to as Program Dependence Graph)
class DependenceGraph {
    friend class SystemDependenceGraph;

    unsigned _id{0};
    // SDG to which this dependence graph belongs
    SystemDependenceGraph *_sdg{nullptr};

    std::vector<std::unique_ptr<DGNode>> _nodes;
    std::vector<std::unique_ptr<DGBBlock>> _bblocks;
    DependenceGraph(unsigned id, SystemDependenceGraph *g)
    : _id(id), _sdg(g) { assert(id > 0); }

    std::string _name;

public:
    unsigned getID() const { return _id; }
    SystemDependenceGraph *getSDG() { return _sdg; }
    const SystemDependenceGraph *getSDG() const { return _sdg; }

    void setName(const std::string& nm) { _name = nm; }
    const std::string& getName() const { return _name; }

    DGNode *createNode() {
        _nodes.emplace_back(new DGNodeInstruction(_nodes.size() + 1));
        // FIXME: set the dg
        // FIXME: id is local to dg, but we want also a global id...
        // that would be dg.id && node.id?
        return _nodes.back().get();
    }

    DGBBlock *createBBlock() {
        _bblocks.emplace_back(new DGBBlock(_bblocks.size() + 1, this));
        return _bblocks.back().get();
    }
};

} // namespace sdg
} // namespace dg

#endif // DG_DEPENDENCE_GRAPH_H_
