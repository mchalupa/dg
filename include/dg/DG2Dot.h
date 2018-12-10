#ifndef DG_2_DOT_H_
#define DG_2_DOT_H_

#include <iostream>
#include <fstream>
#include <set>

#include "DependenceGraph.h"
#include "analysis/DFS.h"

namespace dg {
namespace debug {

enum dg2dot_options {
    PRINT_NONE      = 0, // print no edges
    PRINT_CFG       = 1 << 0,
    PRINT_REV_CFG   = 1 << 1,
    PRINT_DD        = 1 << 2,
    PRINT_REV_DD    = 1 << 3,
    PRINT_USE       = 1 << 4,
    PRINT_USER      = 1 << 5,
    PRINT_CD        = 1 << 6,
    PRINT_REV_CD    = 1 << 7,
    PRINT_CALL      = 1 << 8,
    PRINT_POSTDOM   = 1 << 9,
    PRINT_ALL       = 0xff
};

struct Indent
{
    int ind;
    Indent(int ind = 1):ind(ind) {}
    friend std::ostream& operator <<(std::ostream& os, const Indent& ind);
};

std::ostream& operator <<(std::ostream& os, const Indent& ind)
{
    for (int i = 0; i < ind.ind; ++i)
        os << "\t";

    return os;
}

template <typename NodeT>
class DG2Dot
{
    std::set<const typename DependenceGraph<NodeT>::ContainerType *> dumpedGlobals;
    // slicing criteria
    std::set<NodeT *> criteria;
public:
    using KeyT = typename NodeT::KeyType;

    DG2Dot<NodeT>(DependenceGraph<NodeT> *dg,
                  uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD | PRINT_USE,
                  const char *file = NULL)
        : options(opts), dg(dg), file(file)
    {
        // if a graph has no global nodes, this will forbid trying to print them
        dumpedGlobals.insert(nullptr);
        reopen(file);
    }

    void setSlicingCriteria(const std::set<NodeT *>& crit) {
        criteria = crit;
    }

    bool open(const char *new_file)
    {
        if (out.is_open()) {
            std::cerr << "File already opened (" << file << ")"
                      << std::endl;
            return false;
        } else
            reopen(new_file);
    }

    virtual std::ostream& printKey(std::ostream& os, KeyT key)
    {
        os << key;
        return os;
    }

    // \return - error state: true if there's an error, false otherwise
    virtual bool checkNode(std::ostream& os, NodeT *node)
    {
	    bool err = false;

	    if (!node->getBBlock()) {
	        err = true;
	        os << "\\nERR: no BB";
	    }

	    return err;
    }

    bool ensureFile(const char *fl)
    {
        if (fl)
            reopen(fl);

        if (!out.is_open()) {
            std::cerr << "File '" << file << "' not opened"
                      << std::endl;
            return false;
        }

        return true;
    }

    virtual bool dump(const char *new_file = nullptr,
                      const char *only_functions = nullptr)
    {
        (void) only_functions;

        if (!ensureFile(new_file))
            return false;

        start();

#ifdef ENABLE_CFG
        dumpBBs(dg);
#endif

        // even when we have printed nodes while
        // going through BBs, print nodes again,
        // so that we'll see if there are any nodes
        // that are not in BBs
        dump_nodes();
        dump_edges();

        // print subgraphs once we printed all the nodes
        if (!subgraphs.empty())
            out << "\n\t/* ----------- SUBGRAPHS ---------- */\n\n";
        for (auto sub : subgraphs) {
            dump_subgraph(sub);
        }


        end();

        out.close();
        return true;
    }

    /* if user want's manual printing, he/she can */

    void start()
    {
        out << "digraph \"DependenceGraph\" {\n";
        out << "\tcompound=true label=\"Graph " << dg
            << " has " << dg->size() << " nodes\\n\n"
            << "\tdd edges color: " << dd_color << "\n"
            << "\tuse edges color: " << use_color << ", dashed\n"
            << "\tcd edges color: " << cd_color << "\n"
            << "\tcfg edges color: " << cfg_color << "\"\n\n";
    }

    void end()
    {
        out << "}\n";
    }

    void dumpSubgraphStart(DependenceGraph<NodeT> *sub,
                           const char *name = nullptr)
    {
        out << "\t/* subgraph " << sub << " nodes */\n";
        out << "\tsubgraph cluster_" << sub << " {\n";
        out << "\t\tstyle=\"filled, rounded\" fillcolor=gray95\n";
        out << "\t\tlabel=\"Subgraph ";
        if (name)
            out << name << " ";

        out << "[" << sub << "]"
            << "\\nhas " << sub->size() << " nodes\n";

        uint64_t slice_id = sub->getSlice();
        if (slice_id != 0)
            out << "\\nslice: "<< slice_id;

        out << "\"\n";


        // dump BBs of the formal parameters
        dump_parameters(sub, 2);
    }

    void dumpSubgraphEnd(DependenceGraph<NodeT> *sub, bool with_nodes = true)
    {
        if (with_nodes) {
            // dump all nodes, to get it without BBlocks
            // (we may not have BBlocks or we just don't want
            // to print them
            for (auto& I : *sub) {
                dump_node(I.second, 2);
                dump_node_edges(I.second, 2);
            }

            if (dumpedGlobals.insert(sub->getGlobalNodes().get()).second) {
                for (auto& I : *sub->getGlobalNodes()) {
                    dump_node(I.second, 2, "GLOB");
                    dump_node_edges(I.second, 2);
                }
            }
        }

        out << "\t}\n";
    }

    void dumpSubgraph(DependenceGraph<NodeT> *sub)
    {
        dumpSubgraphStart(sub);
        dumpSubgraphEnd(sub);
    }

    void dumpBBlock(BBlock<NodeT> *BB, int ind = 2)
    {
        dumpBB(BB, ind);
    }

    void dumpBBlockEdges(BBlock<NodeT> *BB, int ind = 1)
    {
        dumpBBedges(BB, ind);
    }

private:
    // what all to print?
    uint32_t options;

    void reopen(const char *new_file)
    {
        if (!new_file)
            new_file = "/dev/stdout";

        if (out.is_open())
            out.close();

        out.open(new_file);
        file = new_file;
    }

    void dumpBB(const BBlock<NodeT> *BB, int indent)
    {
        Indent Ind(indent);

        out << Ind << "/* Basic Block ";
        printKey(out, BB->getKey());
        out << " [" << BB << "] */\n";
        out << Ind << "subgraph cluster_bb_" << BB << " {\n";
        out << Ind << "\tstyle=filled fillcolor=white\n";
        out << Ind << "\tlabel=\"";

        printKey(out, BB->getKey());
        out << " [" << BB << "]";

        unsigned int dfsorder = BB->getDFSOrder();
        if (dfsorder != 0)
            out << Ind << "\\ndfs order: "<< dfsorder;

        uint64_t slice_id = BB->getSlice();
        if (slice_id != 0)
            out << "\\nslice: "<< slice_id;

        out << "\"\n";

        for (NodeT *n : BB->getNodes()) {
            // print nodes in BB, edges will be printed later
            out << Ind << "\tNODE" << n
                << " [label=\"" << n->getKey() << "\"]\n";
        }

        out << Ind << "} /* cluster_bb_" << BB << " */\n\n";
    }

    void dumpBBedges(BBlock<NodeT> *BB, int indent)
    {
        Indent Ind(indent);

        if (options & PRINT_CFG) {
            for (auto S : BB->successors()) {
                NodeT *lastNode = BB->getLastNode();
                NodeT *firstNode = S.target->getFirstNode();

                out << Ind
                    << "NODE" << lastNode << " -> "
                    <<   "NODE" << firstNode
                    << " [penwidth=2 label=\"" << static_cast<int>(S.label) << "\""
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S.target
                    << "  color=\"" << cfg_color << "\"" << "]\n";
            }
        }

        if (options & PRINT_REV_CFG) {
            for (auto S : BB->predecessors()) {
                NodeT *lastNode = S->getLastNode();
                NodeT *firstNode = BB->getFirstNode();

                out << Ind
                    << "NODE" << firstNode << " -> "
                    <<   "NODE" << lastNode
                    << " [penwidth=2 color=\"" << cfg_color << "\" dashed"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S << " constraint=false]\n";
            }
        }

        if (options & PRINT_CD) {
            for (auto S : BB->controlDependence()) {
                NodeT *lastNode = BB->getLastNode();
                NodeT *firstNode = S->getFirstNode();

                out << Ind
                    << "NODE" << lastNode << " -> "
                    <<   "NODE" << firstNode
                    << " [penwidth=2 color=blue"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S << "]\n";
            }

            for (BBlock<NodeT> *S : BB->getPostDomFrontiers()) {
                NodeT *start = BB->getFirstNode();
                NodeT *end = S->getLastNode();

                out << Ind
                    << "/* post-dominance frontiers */\n"
                    << "NODE" << start << " -> "
                    <<   "NODE" << end
                    << " [penwidth=3 color=green"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S << " constraint=false]\n";
            }
        }

        if (options & PRINT_POSTDOM) {
            BBlock<NodeT> *ipd = BB->getIPostDom();
            if (ipd) {
                NodeT *firstNode = BB->getFirstNode();
                NodeT *lastNode = ipd->getLastNode();

                out << Ind
                    << "NODE" << lastNode << " -> "
                    <<   "NODE" << firstNode
                    << " [penwidth=3 color=purple"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << ipd << " constraint=false]\n";
            }
        }
    }

    void dump_parameters(NodeT *node, int ind)
    {
        DGParameters<NodeT> *params = node->getParameters();

        if (params) {
            dump_parameters(params, ind, false);
        }
    }

    void dump_parameters(DependenceGraph<NodeT> *g, int ind)
    {
        DGParameters<NodeT> *params = g->getParameters();

        if (params) {
            dump_parameters(params, ind, true);
        }
    }

    void dump_parameters(DGParameters<NodeT> *params, int ind, bool formal)
    {
        Indent Ind(ind);

        // FIXME
        // out << Ind << "/* Input parameters */\n";
        // dumpBB(params->getBBIn(), data);
        // out << Ind << "/* Output parameters */\n";
        // dumpBB(params->getBBOut(), data);

        // dump all the nodes again to get the names
        for (auto it : *params) {
            auto& p = it.second;
            if (p.in) {
                dump_node(p.in, ind, formal ? "[f] IN ARG" : "IN ARG");
                dump_node_edges(p.in, ind);
            } else
                out << "NO IN ARG";

            if (p.out) {
                dump_node(p.out, ind, formal ? "[f] OUT ARG" : "OUT ARG");
                dump_node_edges(p.out, ind);
            } else
                out << "NO OUT ARG";
        }

        for (auto I = params->global_begin(), E = params->global_end();
             I != E; ++I) {
            auto& p = I->second;
            if (p.in) {
                dump_node(p.in, ind, formal ? "[f] GLOB IN" : "GLOB IN");
                dump_node_edges(p.in, ind);
            } else
                out << "NO GLOB IN ARG";

            if (p.out) {
                dump_node(p.out, ind, formal ? "[f] GLOB OUT" : "GLOB OUT");
                dump_node_edges(p.out, ind);
            } else
                out << "NO GLOB OUT ARG";
        }

        auto p = params->getVarArg();
        if (p) {
            if (p->in) {
                dump_node(p->in, ind, "[va] IN ARG");
                dump_node_edges(p->in, ind);
            } else
                out << "NO IN va ARG";

            if (p->out) {
                dump_node(p->out, ind, "[va] OUT ARG");
                dump_node_edges(p->out, ind);
            } else
                out << "NO OUT ARG";
        }

        if (auto noret = params->getNoReturn()) {
            dump_node(noret, ind, "[noret]");
            dump_node_edges(noret, ind);
        }
    }

    void dump_subgraph(DependenceGraph<NodeT> *sub)
    {
        dumpSubgraphStart(sub);

#ifdef ENABLE_CFG
        // dump BBs in the subgraph
        dumpBBs(sub, 2);
#endif

        // dump all nodes again, if there is any that is
        // not in any BB
        for (auto& I : *sub)
            dump_node(I.second, 2);
        // dump edges between nodes
        for (auto& I : *sub)
            dump_node_edges(I.second, 2);

        dumpSubgraphEnd(sub);
    }

    void dumpBBs(DependenceGraph<NodeT> *graph, int ind = 1)
    {
        for (auto it : graph->getBlocks())
            dumpBB(it.second, ind);

        // print CFG edges between BBs
        if (options & (PRINT_CFG | PRINT_REV_CFG)) {
            out << Indent(ind) << "/* CFG edges */\n";
            for (auto it : graph->getBlocks())
                dumpBBedges(it.second, ind);
        }
    }

    void dump_node(NodeT *node, int ind = 1, const char *prefix = nullptr)
    {
        bool err = false;
        unsigned int dfsorder = node->getDFSOrder();
        unsigned int bfsorder = node->getDFSOrder();
        uint32_t slice_id = node->getSlice();
        Indent Ind(ind);

        out << Ind
            << "NODE" << node << " [label=\"";

        if (prefix)
            out << prefix << " ";

        printKey(out, node->getKey());

        if (node->hasSubgraphs())
            out << "\\nsubgraphs: " << node->subgraphsNum();
        if (dfsorder != 0)
            out << "\\ndfs order: "<< dfsorder;
        if (bfsorder != 0)
            out << "\\nbfs order: "<< bfsorder;

        if (slice_id != 0)
            out << "\\nslice: "<< slice_id;

        // check if the node is OK, and if not
        // highlight it
        err = checkNode(out, node);

        // end of label
        out << "\" ";

        if (err) {
            out << "style=filled fillcolor=red";
        } else if (criteria.count(node) > 0) {
            out << "style=filled fillcolor=orange";
        } else if (slice_id != 0)
            out << "style=filled fillcolor=greenyellow";
        else
            out << "style=filled fillcolor=white";

        out << "]\n";

        dump_parameters(node, ind);
        if (node->hasSubgraphs() && (options & PRINT_CALL)) {
            // add call-site to callee edges
            for (auto subgraph : node->getSubgraphs()) {
                out << Ind
                    << "NODE" << node
                    << " -> NODE" << subgraph->getEntry()
                    << " [label=\"call\""
                    << "  lhead=cluster_" << subgraph
                    << " penwidth=3 style=dashed]\n";
            }
        }
    }

    void dump_nodes()
    {
        out << "\t/* nodes */\n";
        for (auto& I : *dg) {
            auto node = I.second;

            dump_node(node);

            for (auto subgraph : node->getSubgraphs()) {
                subgraphs.insert(subgraph);
            }
        }

        if (dumpedGlobals.insert(dg->getGlobalNodes().get()).second)
            for (auto& I : *dg->getGlobalNodes())
                dump_node(I.second, 1, "GL");
    }

    void dump_edges()
    {
        for (auto& I : *dg) {
            dump_node_edges(I.second);
        }

        if (dumpedGlobals.insert(dg->getGlobalNodes().get()).second)
            for (auto& I : *dg->getGlobalNodes())
                dump_node_edges(I.second);
    }

    void dump_node_edges(NodeT *n, int ind = 1)
    {
        Indent Ind(ind);

        out << Ind << "/* -- node " << n->getKey() << "\n"
            << Ind << " * ------------------------------------------- */\n";

        if (options & PRINT_DD) {
            out << Ind << "/* DD edges */\n";
            for (auto II = n->data_begin(), EE = n->data_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << dd_color << "\" rank=max]\n";
        }

        if (options & PRINT_REV_DD) {
            out << Ind << "/* reverse DD edges */\n";
            for (auto II = n->rev_data_begin(), EE = n->rev_data_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << dd_color << "\" style=\"dashed\"  constraint=false]\n";
        }

        if (options & PRINT_USE) {
            out << Ind << "/* USE edges */\n";
            for (auto II = n->use_begin(), EE = n->use_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << use_color << "\" rank=max style=\"dashed\"]\n";
        }

        if (options & PRINT_USER) {
            out << Ind << "/* user edges */\n";
            for (auto II = n->user_begin(), EE = n->user_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << use_color << "\" style=\"dashed\"  constraint=false]\n";
        }


        if (options & PRINT_CD) {
            out << Ind << "/* CD edges */\n";
            for (auto II = n->control_begin(), EE = n->control_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << cd_color << "\"]\n";
        }

        if (options & PRINT_REV_CD) {
            out << Ind << "/* reverse CD edges */\n";
            for (auto II = n->rev_control_begin(), EE = n->rev_control_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << cd_color << "\" style=\"dashed\" constraint=false]\n";
        }
    }

    const char *dd_color = "cyan4";
    const char *use_color = "black";
    const char *cd_color = "blue";
    const char *cfg_color = "gray";

    DependenceGraph<NodeT> *dg;
    const char *file;
    std::set<DependenceGraph<NodeT> *> subgraphs;

protected:
    std::ofstream out;
};

} // debug
} // namespace dg

#endif // DG_2_DOT_H_

