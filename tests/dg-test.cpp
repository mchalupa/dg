#include "../src/DependenceGraph.h"
#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "../src/LLVMDependenceGraph.h"

using namespace dg;

class TestDG : public DependenceGraph<int>
{
};

static unsigned int id = 0;

class TestNode : public DGNode<int>
{
public:
    TestNode() :DGNode<int>(++id), dfs_visited(0) {};

    unsigned int dfs_visited;
};

static void dump_to_dot(const DGNode<int> *n, FILE *f)
{
    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p;\n", n, *I);
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p [color=red];\n", n, *I);
}

template<typename Key>
void print_to_dot(DependenceGraph<Key> *dg,
                  const char *file = "last_test.dot",
                  const char *description = NULL)
{
    // we have stdio included, do not use streams for such
    // easy task
    FILE *out = fopen(file, "w");
    if (!out) {
        fprintf(stderr, "Failed opening file %s\n", file);
        return;
    }

    fprintf(out, "digraph \"%s\" {\n",
            description ? description : "DependencyGraph");

    // if we have entry node, use it as a root
    // otherwise just dump the graph somehow
    if (dg->getEntry()) {
        dg->DFS(dg->getEntry(), dump_to_dot, out);
    } else {
        for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
            dump_to_dot(I->second, out);
        }
    }

    fprintf(out, "}\n");
    fclose(out);
}


/* return true when expr is violated and false when
 * it is OK */
static bool check(int expr, const char *func, int line, const char *fmt, ...)
{
    va_list args;

    if (expr)
        return false;

    fprintf(stderr, "%s:%d - ", func, line);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);

    return true;
}

#define chck_init() bool __chck_ret = false;
// return false when some check failed
#define chck_ret() return __chck_ret;
#define chck_dump(d)\
    do { if (__chck_ret) \
        {print_to_dot((d)); }\
    } while(0)
#define chck(expr, ...)    \
    do { __chck_ret |= check((expr), __func__, __LINE__, __VA_ARGS__); } while(0)

static bool constructors_test(void)
{
    chck_init();

    TestDG d;

    chck(d.getEntry() == NULL, "BUG: garbage in entry");
    chck(d.getSize() == 0, "BUG: garbage in nodes_num");

    TestNode n;

    chck(n.getSubgraph() == NULL, "BUG: garbage in subgraph");
    chck(n.getParameters() == NULL, "BUG: garbage in parameters");

    chck_dump(&d);
    chck_ret();
}

static bool add_test1(void)
{
    chck_init();

    TestDG d;
    TestNode n1, n2;

    chck(n1.addControlEdge(&n2), "adding C edge claims it is there");
    chck(n2.addDependenceEdge(&n1), "adding D edge claims it is there");

    d.addNode(&n1);
    d.addNode(&n2);

    d.setEntry(&n1);
    chck(d.getEntry() == &n1, "BUG: Entry setter");

    int n = 0;
    /*
    for (auto I = d.begin(), E = d.end(); I != E; ++I) {
        ++n;
        chck(*I == &n1 || *I == &n2, "Got some garbage in nodes");
    }
    */

    chck(n == 2, "BUG: adding nodes to graph, got %d instead of 2", n);

    int nn = 0;
    for (auto ni = n1.control_begin(), ne = n1.control_end(); ni != ne; ++ni){
        chck(*ni == &n2, "got wrong control edge");
        ++nn;
    }

    chck(nn == 1, "bug: adding control edges, has %d instead of 1", nn);

    nn = 0;
    for (auto ni = n2.dependence_begin(), ne = n2.dependence_end();
         ni != ne; ++ni) {
        chck(*ni == &n1, "got wrong control edge");
        ++nn;
    }

    chck(nn == 1, "BUG: adding dep edges, has %d instead of 1", nn);
    chck(d.getSize() == 2, "BUG: wrong nodes num");

    // adding the same node should not increase number of nodes
    chck(!d.addNode(&n1), "should get false when adding same node");
    chck(d.getSize() == 2, "BUG: wrong nodes num (2)");
    chck(!d.addNode(&n2), "should get false when adding same node (2)");
    chck(d.getSize() == 2, "BUG: wrong nodes num (2)");

    // don't trust just the counter
    n = 0;
    /*
    for (auto I = d.begin(), E = d.end(); I != E; ++I)
        ++n;
    */

    chck(n == 2, "BUG: wrong number of nodes in graph", n);

    // we're not a multi-graph, each edge is there only once
    // try add multiple edges
    chck(!n1.addControlEdge(&n2),
         "adding multiple C edge claims it is not there");
    chck(!n2.addDependenceEdge(&n1),
         "adding multiple D edge claims it is not there");

    nn = 0;
    for (auto ni = n1.control_begin(), ne = n1.control_end(); ni != ne; ++ni){
        chck(*ni == &n2, "got wrong control edge (2)");
        ++nn;
    }

    chck(nn == 1, "bug: adding control edges, has %d instead of 1 (2)", nn);

    nn = 0;
    for (auto ni = n2.dependence_begin(), ne = n2.dependence_end();
         ni != ne; ++ni) {
        chck(*ni == &n1, "got wrong control edge (2) ");
        ++nn;
    }

    chck(nn == 1, "bug: adding dependence edges, has %d instead of 1 (2)", nn);

    chck_dump(&d);
    chck_ret();
}

int main(int argc, char *argv[])
{
    bool ret = false;

    ret |= constructors_test();
    ret |= add_test1();

    return ret;
}
