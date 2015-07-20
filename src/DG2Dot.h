#ifndef DG_2_DOT_H_
#define DG_2_DOT_H_

#include <iostream>
#include <fstream>

#include "DependenceGraph.h"
#include "analysis/DFS.h"

namespace dg {
namespace debug {

enum dg2dot_options {
    PRINT_CFG       = 1 << 0,
    PRINT_DD        = 1 << 1,
    PRINT_REV_DD    = 1 << 2,
    PRINT_CD        = 1 << 3,
    PRINT_REV_CD    = 1 << 4,
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

template <typename KeyT, typename ValueT>
class DG2Dot
{
public:
    DG2Dot<KeyT, ValueT>(DependenceGraph<KeyT, ValueT> *dg,
                         uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD,
                         const char *file = NULL)
        : options(opts), dg(dg), file(file)
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

    bool dump(const char *new_file = NULL, BBlock<ValueT> *BB = nullptr)
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
        else
            dump_nodes();

        //dump_edges();

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

    static void dumpBB(BBlock<ValueT> *BB, std::pair<std::ofstream&, uint32_t> data)
    {
        std::ofstream& out = data.first;
        out << "\t/* BasicBlock " << BB << " */\n";
        out << "\tsubgraph cluster_bb_" << BB << " {\n";
        out << "\t\tlabel=\"Basic Block " << BB << "\"\n";

        ValueT n = BB->getFirstNode();
        while (n) {
            // print nodes in BB, edges will be printed later
            out << "\t\tNODE" << n << " [label=\"" << n->getKey() << "\"]\n";
            n = n->getSuccessor();
        }

        out << "\t} /* cluster_bb_" << BB << " */\n";
    }

    static void dumpBBedges(BBlock<ValueT> *BB, std::pair<std::ofstream&, uint32_t> data)
    {
        std::ofstream& out = data.first;
        for (auto S : BB->successors()) {
            // dot cannot draw arrows between clusters,
            // so draw them between first and last node
            ValueT lastNode = BB->getLastNode();
            ValueT firstNode = S->getFirstNode();

            out << "\tNODE" << lastNode << " -> "
                <<   "NODE" << firstNode
                << "[penwidth=2"
                << " ltail=cluster_bb_" << BB
                << " lhead=cluster_bb_" << S << "]\n";
        }
    }

    void dumpBBs(BBlock<ValueT> *startBB)
    {
        dg::analysis::BBlockDFS<ValueT> DFS;

        // print nodes in BB
        DFS.run(startBB, dumpBB, std::pair<std::ofstream&, uint32_t>(out, options));

        // print CFG edges between BBs
        out << "\t/* CFG edges */\n";
        DFS.run(startBB, dumpBBedges, std::pair<std::ofstream&, uint32_t>(out, options));
    }

    void dump_nodes()
    {
        out << "\t/* nodes */\n";
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
            out << "\tNODE" << I->second << " [label=\"" << I->second->getKey()
                << "\"]\n";
    }

    void dump_edges()
    {
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
            dump_node_edges(I->second);
    }

    void dump_node_edges(ValueT n, int ind = 1)
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

            out << Ind << "/* Predcessor */\n";
            if (n->hasPredcessor()) {
                out << Ind << "NODE" << n << " -> NODE" << n->getPredcessor()
                    << " [style=\"dotted\"]\n";
            }
        }
    }

    const char *dd_color = "black";
    const char *cd_color = "blue";

    DependenceGraph<KeyT, ValueT> *dg;
    const char *file;
    std::ofstream out;
};

} // debug
} // namespace dg

#endif // DG_2_DOT_H_

