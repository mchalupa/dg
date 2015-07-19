#ifndef DG_2_DOT_H_
#define DG_2_DOT_H_

#include <iostream>
#include <fstream>

#include "DependenceGraph.h"

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

    bool dump(const char *new_file = NULL)
    {
        if (new_file)
            reopen(new_file);

        if (!out.is_open()) {
            std::cerr << "File '" << file << "' not opened"
                      << std::endl;
            return false;
        }

        start();
        dump_nodes();
        dump_edges();
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
        out << "\tlabel=\"Graph " << dg
            << " has " << dg->size() << " nodes\\n\n"
            << "\tdd color: " << dd_color << "\n"
            << "\tcd color: " << cd_color << "\"\n\n";
    }

    void end()
    {
        out << "}\n";
    }

    void dump_nodes()
    {
        out << "\t/* nodes */\n";
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
            out << "\tNODE" << I->second
                << " [label=\"" << I->second->getKey()
                << "\"]\n";
    }

    void dump_edges()
    {
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
            ValueT n = I->second;

            out << "\n\n"
                << "\t/* -- node " << n->getKey() << "\n"
                << "\t * ------------------------------------------- */\n";

            if (options & PRINT_DD) {
                out << "\t/* DD edges */\n";
                for (auto II = n->data_begin(), EE = n->data_end();
                     II != EE; ++II)
                    out << "\tNODE" << n << " -> NODE" << *II
                        << " [color=\"" << dd_color << "\"]\n";
            }

            if (options & PRINT_REV_DD) {
                out << "\t/* reverse DD edges */\n";
                for (auto II = n->rev_data_begin(), EE = n->rev_data_end();
                     II != EE; ++II)
                    out << "\tNODE" << n << " -> NODE" << *II
                        << " [color=\"" << dd_color << "\" style=\"dashed\"]\n";
            }

            if (options & PRINT_CD) {
                out << "\t/* CD edges */\n";
                for (auto II = n->control_begin(), EE = n->control_end();
                     II != EE; ++II)
                    out << "\tNODE" << n << " -> NODE" << *II
                        << " [color=\"" << cd_color << "\"]\n";
            }

            if (options & PRINT_REV_CD) {
                out << "\t/* reverse CD edges */\n";
                for (auto II = n->rev_control_begin(), EE = n->rev_control_end();
                     II != EE; ++II)
                    out << "\tNODE" << n << " -> NODE" << *II
                        << " [color=\"" << cd_color << "\" style=\"dashed\"]\n";
            }

            if (options & PRINT_CFG) {
                out << "\t/* Successor */\n";
                if (n->hasSuccessor()) {
                    out << "\tNODE" << n << " -> NODE" << n->getSuccessor() 
                        << " [style=\"dotted\"]\n";
                }

                out << "\t/* Predcessor */\n";
                if (n->hasPredcessor()) {
                    out << "\tNODE" << n << " -> NODE" << n->getPredcessor() 
                        << " [style=\"dotted\"]\n";
                }
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

