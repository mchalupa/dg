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
    PRINT_CD        = 1 << 4,
    PRINT_REV_CD    = 1 << 5,
    PRINT_CALL      = 1 << 6,
#ifdef ENABLE_PSS
    PRINT_PSS       = 1 << 7,
#endif
    PRINT_ALL       = ~((uint32_t) 0)
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
public:
    typedef typename NodeT::KeyType KeyT;

    DG2Dot<NodeT>(DependenceGraph<NodeT> *dg,
                  uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD,
                  const char *file = NULL)
        : options(opts), dg(dg), file(file),
          printKey(nullptr), checkNode(check_node)
    {
        reopen(file);
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

    bool dump(const char *new_file = NULL, BBlock<NodeT> *BB = nullptr)
    {
        if (new_file)
            reopen(new_file);

        if (!out.is_open()) {
            std::cerr << "File '" << file << "' not opened"
                      << std::endl;
            return false;
        }

        start();

        if (BB)
            dumpBBs(BB);

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
    }

    // what all to print?
    uint32_t options;

private:
    void reopen(const char *new_file)
    {
        if (out.is_open())
            out.close();

        out = std::ofstream(new_file);
        file = new_file;
    }

    void start()
    {
        out << "digraph \"DependenceGraph\" {\n";
        out << "\tcompound=true label=\"Graph " << dg
            << " has " << dg->size() << " nodes\\n\n"
            << "\tdd color: " << dd_color << "\n"
            << "\tcd color: " << cd_color << "\"\n\n";
    }

    void end()
    {
        out << "}\n";
    }

    struct DumpBBData {
        DumpBBData(std::ofstream& o, uint32_t opts, int ind = 1)
            : out(o), options(opts), indent(ind) {}

        std::ofstream& out;
        uint32_t options;
        int indent;
    };

    static void dumpBB(const BBlock<NodeT> *BB, DumpBBData& data)
    {
        std::ofstream& out = data.out;
        Indent Ind(data.indent);

        out << Ind << "/* BasicBlock " << BB << " */\n";
        out << Ind << "subgraph cluster_bb_" << BB << " {\n";
        out << Ind << "\tlabel=\"Basic Block " << BB;

        unsigned int dfsorder = BB->getDFSOrder();
        if (dfsorder != 0)
            out << Ind << "\\ndfs order: "<< dfsorder;

        out << "\"\n";

        NodeT *n = BB->getFirstNode();
        while (n) {
            // print nodes in BB, edges will be printed later
            out << Ind << "\tNODE" << n
                << " [label=\"" << n->getKey() << "\"]\n";
            n = n->getSuccessor();
        }

        out << Ind << "} /* cluster_bb_" << BB << " */\n\n";
    }

    static void dumpBBedges(BBlock<NodeT> *BB, DumpBBData& data)
    {
        std::ofstream& out = data.out;
        uint32_t options = data.options;
        Indent Ind(data.indent);

        if (options & PRINT_CFG) {
            for (auto S : BB->successors()) {
                NodeT *lastNode = BB->getLastNode();
                NodeT *firstNode = S->getFirstNode();

                out << Ind
                    << "NODE" << lastNode << " -> "
                    <<   "NODE" << firstNode
                    << " [penwidth=2"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S << "]\n";
            }
        }

        if (options & PRINT_REV_CFG) {
            for (auto S : BB->predcessors()) {
                NodeT *lastNode = S->getLastNode();
                NodeT *firstNode = BB->getFirstNode();

                out << Ind
                    << "NODE" << firstNode << " -> "
                    <<   "NODE" << lastNode
                    << " [penwidth=2 color=gray"
                    << "  ltail=cluster_bb_" << BB
                    << "  lhead=cluster_bb_" << S << "]\n";
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
        }
    }

    void dump_parameters(NodeT *node, int ind)
    {
        DGParameters<KeyT, NodeT> *params = node->getParameters();

        if (params) {
            dump_parameters(params, ind);
        }
    }

    void dump_parameters(DependenceGraph<NodeT> *g, int ind)
    {
        DGParameters<KeyT, NodeT> *params = g->getParameters();

        if (params) {
            dump_parameters(params, ind);
        }
    }

    void dump_parameters(DGParameters<KeyT, NodeT> *params, int ind)
    {
        Indent Ind(ind);
        DumpBBData data(out, options, ind);

        out << Ind << "/* Input parameters */\n";
        dumpBB(params->getBBIn(), data);
        out << Ind << "/* Output parameters */\n";
        dumpBB(params->getBBOut(), data);

        // dump all the nodes again to get the names
        for (auto it : *params) {
            dump_node(it.second.in, ind, "IN ARG");
            dump_node_edges(it.second.in, ind);

            dump_node(it.second.out, ind, "OUT ARG");
            dump_node_edges(it.second.out, ind);
        }
    }


    void dump_subgraph(DependenceGraph<NodeT> *sub)
    {
        out << "\t/* subgraph " << sub << " nodes */\n";
        out << "\tsubgraph cluster_" << sub << " {\n";
        out << "\t\tlabel=\"Subgraph " << sub
            << " has " << sub->size() << " nodes\"\n";

        // dump BBs of the formal parameters
        dump_parameters(sub, 2);

        // dump BBs in the subgraph
        if (sub->getEntryBB())
            dumpBBs(sub->getEntryBB(), 2);

        // dump all nodes again, if there is any that is
        // not in any BB
        for (auto I = sub->begin(), E = sub->end(); I != E; ++I)
            dump_node(I->second, 2);
        // dump edges between nodes
        for (auto I = sub->begin(), E = sub->end(); I != E; ++I)
            dump_node_edges(I->second, 2);

        out << "\t}\n";
    }

    void dumpBBs(BBlock<NodeT> *startBB, int ind = 1)
    {
        dg::analysis::BBlockDFS<NodeT> DFS;

        // print nodes in BB
        DFS.run(startBB, dumpBB, DumpBBData(out, options, ind));

        // print CFG edges between BBs
        if (options & (PRINT_CFG | PRINT_REV_CFG)) {
            out << Indent(ind) << "/* CFG edges */\n";
            DFS.run(startBB, dumpBBedges, DumpBBData(out, options, ind));
        }
    }

	static bool check_node(std::ostream& os, NodeT *node)
	{
	    bool err = false;

	    if (!node->getBasicBlock()) {
	        err = true;
	        os << "\\nERR: no BB";
	    }

	    return err;
	}

    void dump_node(NodeT *node, int ind = 1, const char *prefix = nullptr)
    {
        bool err = false;
        unsigned int dfsorder = node->getDFSOrder();
        uint32_t slice_id = node->getSlice();
        Indent Ind(ind);

        out << Ind
            << "NODE" << node << " [label=\"";

        if (prefix)
            out << prefix << " ";

        if (printKey)
            printKey(out, node->getKey());
        else
            out << node->getKey();

        if (dfsorder != 0)
            out << "\\ndfs order: "<< dfsorder;

        if (slice_id != 0)
            out << "\\nslice: "<< slice_id;

        // check if the node is OK, and if not
        // highlight it
        err = checkNode(out, node);

        // end of label
        out << "\"";

        if (err) {
            out << "style=filled fillcolor=red";
        } else if (slice_id != 0)
            out << "style=filled fillcolor=gray";

        out << "]\n";

        dump_parameters(node, ind);
        if (node->hasSubgraphs() && (options & PRINT_CALL)) {
            // add call-site to callee edges
            for (auto I = node->getSubgraphs().begin(),
                      E = node->getSubgraphs().end(); I != E; ++I) {
                out << Ind
                    << "NODE" << node
                    << " -> NODE" << (*I)->getEntry()
                    << " [label=\"call\""
                    << "  lhead=cluster_" << *I
                    << " penwidth=3 style=dashed]\n";
            }
        }
    }

    void dump_nodes()
    {
        out << "\t/* nodes */\n";
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
            auto node = I->second;

            dump_node(node);

            for (auto I = node->getSubgraphs().begin(),
                      E = node->getSubgraphs().end(); I != E; ++I) {
                subgraphs.insert(*I);
            }
        }

        if (dg->ownsGlobalNodes())
            for (auto I : *dg->getGlobalNodes())
                dump_node(I.second, 1, "GL");
    }

    void dump_edges()
    {
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
            dump_node_edges(I->second);

        if (dg->ownsGlobalNodes())
            for (auto I : *dg->getGlobalNodes())
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
                    << " [color=\"" << dd_color << "\"]\n";
        }

        if (options & PRINT_REV_DD) {
            out << Ind << "/* reverse DD edges */\n";
            for (auto II = n->rev_data_begin(), EE = n->rev_data_end();
                 II != EE; ++II)
                out << Ind << "NODE" << n << " -> NODE" << *II
                    << " [color=\"" << dd_color << "\" style=\"dashed\"]\n";
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
                    << " [color=\"" << cd_color << "\" style=\"dashed\"]\n";
        }

        if (options & PRINT_CFG) {
            out << Ind << "/* Successor */\n";
            if (n->hasSuccessor()) {
                out << Ind << "NODE" << n << " -> NODE" << n->getSuccessor()
                    << " [style=\"dotted\"]\n";
            }
        }

        if (options & PRINT_REV_CFG) {
            out << Ind << "/* Predcessor */\n";
            if (n->hasPredcessor()) {
                out << Ind << "NODE" << n << " -> NODE" << n->getPredcessor()
                    << " [style=\"dotted\" color=gray]\n";
            }
        }

#ifdef ENABLE_PSS
        if (options & PRINT_PSS) {
            out << Ind << "/* PSS Edges */\n";
            for (NodeT *edg : n->getPSSEdges())
                out << Ind << "NODE" << n << " -> NODE" << edg
                    << " [style=\"solid\" color=purple penwidth=2]\n";
        }
#endif
    }

    const char *dd_color = "black";
    const char *cd_color = "blue";

    DependenceGraph<NodeT> *dg;
    const char *file;
    std::ofstream out;
    std::set<DependenceGraph<NodeT> *> subgraphs;

public:
    /* functions for adjusting the output. These are public,
     * so that user can set them as he/she wants */
    std::ostream& (*printKey)(std::ostream& os, KeyT key);
	bool (*checkNode)(std::ostream& os, NodeT *node);
};

} // debug
} // namespace dg

#endif // DG_2_DOT_H_

