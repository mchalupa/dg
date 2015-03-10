// XXX license

#include <cassert>
#include <cstdio>
#include <set>

#include "DependenceGraph.h"
#include "DGUtil.h"


/*
static void dump_to_dot(const DGNode<Key> *n, FILE *f)
{
    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p;\n", n, *I);
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p [color=red];\n", n, *I);
}

template <typename Key>
bool DependenceGraph<Key>::dumpToDot(const char *file,
                                     const char *description)
{
    // we have stdio included, do not use streams for such
    // easy task
    FILE *out = fopen(file, "w");
    if (!out) {
        fprintf(stderr, "Failed opening file %s\n", file);
        return false;
    }

    fprintf(out, "digraph \"%s\" {\n",
            description ? description : "DependencyGraph");

    // if we have entry node, use it as a root
    // otherwise just dump the graph somehow
    if (entryNode) {
        DFS(entryNode, dump_to_dot, out);
    } else {
        for (DGNode *n : nodes) {
            dump_to_dot(n, out);
        }
    }

    fprintf(out, "}\n");
    fclose(out);
}
*/
