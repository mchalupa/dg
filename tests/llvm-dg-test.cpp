#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "../src/LLVMDependenceGraph.h"

using namespace dg;

static void dump_to_dot(const LLVMDGNode *n, FILE *f)
{
    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s;\n", n->getValue(), (*I)->getValue());
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s [color=red];\n", n->getValue(), (*I)->getValue());
}

void print_to_dot(LLVMDependenceGraph *dg,
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

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
    {
        auto n = I->second;

        fprintf(out, "\t%s [label=\"%s (runid=%d)\"];\n",
                n->getValue(), n->getValue(), n->getDFSrun());
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
    LLVMDependenceGraph d;

    chck_dump(&d);
    chck_ret();
}

static bool add_test1(void)
{
    chck_init();
    LLVMDependenceGraph d;

    chck_dump(&d);
    chck_ret();
}

static bool refcount_test(void)
{
    chck_init();
    LLVMDependenceGraph d;
    LLVMDependenceGraph s;

    int rc;
    rc = s.ref();
    chck(rc == 2, "refcount shold be 2, but is %d", rc);
    rc = s.unref();
    chck(rc == 1, "refcount shold be 1, but is %d", rc);

    s.ref();
    rc = s.ref();
    chck(rc == 3, "refcount shold be 3, but is %d", rc);
    s.unref();
    rc = s.unref();
    chck(rc == 1, "refcount shold be 1, but is %d", rc);

    // addSubgraph increases refcount
    LLVMDGNode n1(nullptr), n2(nullptr);
    n1.addSubgraph(&s);
    n2.addSubgraph(&s);

    // we do not have a getter for refcounts, so just
    // inc and dec the counter to get the current value
    s.ref();
    rc = s.unref();
    chck(rc == 3, "refcount shold be 3, but is %d", rc);

    chck_ret();
}

int main(int argc, char *argv[])
{
    bool ret = false;

    ret |= constructors_test();
    ret |= refcount_test();

    return ret;
}
