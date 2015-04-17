#include "../src/DependenceGraph.h"
#include <assert.h>
#include <cstdarg>
#include <cstdio>

using namespace dg;

class TestDG;
class TestNode;

class TestNode : public Node<TestDG, TestNode *>
{
    const char *name;
public:
    TestNode(const char *name) : name(name) {};
    const char *getName() const { return name; }
};

class TestDG : public DependenceGraph<const char *, TestNode *>
{
public:
#ifdef ENABLE_CFG
    typedef BBlock<TestNode *> BasicBlock;
#endif // ENABLE_CFG

    bool addNode(TestNode *n) { return DependenceGraph<const char *, TestNode *>::addNode(n->getName(), n); }
};

#define CREATE_NODE(n) TestNode n(#n)

static void dump_to_dot(TestNode *n, FILE *f)
{
    for (TestNode::ControlEdgesType::const_iterator I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s;\n", n->getName(), (*I)->getName());
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s [color=red];\n", n->getName(), (*I)->getName());
}

void print_to_dot(TestDG *dg,
                  const char *file = "last_test.dot",
                  const char *description = nullptr)
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

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
    {
        auto n = I->second;

        fprintf(out, "\t%s [label=\"%s (runid=%d)\"];\n",
                n->getName(), n->getName(), n->getDFSrun());
    }

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

    chck(d.getEntry() == nullptr, "BUG: garbage in entry");
    chck(d.getSize() == 0, "BUG: garbage in nodes_num");

    //TestNode n;
    CREATE_NODE(n);

    chck(!n.hasSubgraphs(), "BUG: garbage in subgraph");
    chck(n.subgraphsNum() == 0, "BUG: garbage in subgraph");
    chck(n.getParameters() == nullptr, "BUG: garbage in parameters");

    chck_dump(&d);
    chck_ret();
}

static bool add_test1(void)
{
    chck_init();

    TestDG d;
    //TestNode n1, n2;
    CREATE_NODE(n1);
    CREATE_NODE(n2);

    chck(n1.addControlDependence(&n2), "adding C edge claims it is there");
    chck(n2.addDataDependence(&n1), "adding D edge claims it is there");

    d.addNode(&n1);
    d.addNode(&n2);

    d.setEntry(&n1);
    chck(d.getEntry() == &n1, "BUG: Entry setter");

    int n = 0;
    for (auto I = d.begin(), E = d.end(); I != E; ++I) {
        ++n;
        chck((*I).second == &n1 || (*I).second == &n2, "Got some garbage in nodes");
    }

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
    for (auto I = d.begin(), E = d.end(); I != E; ++I)
        ++n;

    chck(n == 2, "BUG: wrong number of nodes in graph", n);

    // we're not a multi-graph, each edge is there only once
    // try add multiple edges
    chck(!n1.addControlDependence(&n2),
         "adding multiple C edge claims it is not there");
    chck(!n2.addDataDependence(&n1),
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

static void dfs_do_nothing(TestNode *n, int)
{
}

static bool dfs_test1(void)
{
    chck_init();

    TestDG d;
    //TestNode n1, n2, n3;
    CREATE_NODE(n1);
    CREATE_NODE(n2);
    CREATE_NODE(n3);

    n1.addControlDependence(&n2);
    n2.addDataDependence(&n1);
    n2.addDataDependence(&n3);
    d.addNode(&n1);
    d.addNode(&n2);
    d.addNode(&n3);

    chck(d.getSize() == 3, "BUG: adding nodes");

    unsigned run_id1 = n1.getDFSrun();
    unsigned run_id2 = n2.getDFSrun();
    unsigned run_id3 = n3.getDFSrun();

    chck(run_id1 == run_id2, "garbage in run id");
    chck(run_id2 == run_id3, "garbage in run id");

    // traverse only control edges
    d.DFS(&n1, dfs_do_nothing, 0, true, false);
    chck(run_id1 + 1 == n1.getDFSrun(), "did not go through node 1");
    chck(run_id2 + 1 == n2.getDFSrun(), "did not go through node 2");
    chck(run_id3 == n3.getDFSrun(), "did go through node 3");

    chck_dump(&d);
    chck_ret();
}

static bool cfg_test1(void)
{
    chck_init();

#if ENABLE_CFG

    TestDG d;
    //TestNode n1, n2;
    CREATE_NODE(n1);
    CREATE_NODE(n2);

    d.addNode(&n1);
    d.addNode(&n2);

    chck(!n1.hasSuccessor(), "hasSuccessor returned true on node without successor");
    chck(!n2.hasSuccessor(), "hasSuccessor returned true on node without successor");
    chck(!n1.hasPredcessor(), "hasPredcessor returned true on node without successor");
    chck(!n2.hasPredcessor(), "hasPredcessor returned true on node without successor");
    chck(n1.getSuccessor() == nullptr, "succ initialized with garbage");
    chck(n2.getSuccessor() == nullptr, "succ initialized with garbage");
    chck(n1.getPredcessor() == nullptr, "pred initialized with garbage");
    chck(n2.getPredcessor() == nullptr, "pred initialized with garbage");

    chck(n1.addSuccessor(&n2) == nullptr, "adding successor edge claims it is there");
    chck(n1.hasSuccessor(), "hasSuccessor returned false");
    chck(!n1.hasPredcessor(), "hasPredcessor returned true");
    chck(n2.hasPredcessor(), "hasPredcessor returned false");
    chck(!n2.hasSuccessor(), "hasSuccessor returned false");
    chck(n1.getSuccessor() == &n2, "get/addSuccessor bug");
    chck(n2.getPredcessor() == &n1, "get/addPredcessor bug");

    // basic blocks
    TestDG::BasicBlock BB(&n1);
    chck(BB.getFirstNode() == &n1, "first node incorrectly set");
    chck(BB.setLastNode(&n2) == nullptr, "garbage in lastNode");
    chck(BB.getLastNode() == &n2, "bug in setLastNode");

    chck(BB.successorsNum() == 0, "claims: %u", BB.successorsNum());
    chck(BB.predcessorsNum() == 0, "claims: %u", BB.predcessorsNum());

    CREATE_NODE(n3);
    CREATE_NODE(n4);
    d.addNode(&n3);
    d.addNode(&n4);

    TestDG::BasicBlock BB2(&n3), BB3(&n3);

    chck(BB.addSuccessor(&BB2), "the edge is there");
    chck(!BB.addSuccessor(&BB2), "added even when the edge is there");
    chck(BB.addSuccessor(&BB3), "the edge is there");
    chck(BB.successorsNum() == 2, "claims: %u", BB.successorsNum());

    chck(BB2.predcessorsNum() == 1, "claims: %u", BB2.predcessorsNum());
    chck(BB3.predcessorsNum() == 1, "claims: %u", BB3.predcessorsNum());
    chck(*(BB2.predcessors().begin()) == &BB, "wrong predcessor set");
    chck(*(BB3.predcessors().begin()) == &BB, "wrong predcessor set");

    for (auto s : BB.successors())
        chck(s == &BB2 || s == &BB3, "Wrong succ set");

    BB2.removePredcessors();
    chck(BB.successorsNum() == 1, "claims: %u", BB.successorsNum());
    chck(BB2.predcessorsNum() == 0, "has successors after removing");

    BB.removeSuccessors();
    chck(BB.successorsNum() == 0, "has successors after removing");
    chck(BB2.predcessorsNum() == 0, "removeSuccessors did not removed BB"
                                    " from predcessor");
    chck(BB3.predcessorsNum() == 0, "removeSuccessors did not removed BB"
                                    " from predcessor");

    chck_dump(&d);
#endif

    chck_ret();
}

int main(int argc, char *argv[])
{
    bool ret = false;

    ret |= constructors_test();
    ret |= add_test1();
    ret |= dfs_test1();
    ret |= cfg_test1();

    return ret;
}
