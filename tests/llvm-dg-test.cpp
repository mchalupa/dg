#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "../src/LLVMDependenceGraph.h"

using namespace dg;

static void dump_to_dot(const DGNode<const llvm::Value *> *n, FILE *f)
{
    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s;\n", n->getKey(), (*I)->getKey());
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\t%s -> %s [color=red];\n", n->getKey(), (*I)->getKey());
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

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
    {
        auto n = I->second;

        fprintf(out, "\t%s [label=\"%s (runid=%d)\"];\n",
                n->getKey(), n->getKey(), n->getDFSrun());
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

int main(int argc, char *argv[])
{
    bool ret = false;

    ret |= constructors_test();

    return ret;
}
