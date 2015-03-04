#include "../src/DependenceGraph.h"
#include <assert.h>
#include <cstdarg>
#include <cstdio>

using namespace dg;

static bool check(int expr, const char *fmt, ...)
{
	va_list args;

	if (expr)
		return false;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fputc('\n', stderr);

	return true;
}

#define chck_init() bool __chck_ret = false;
// return false when some check failed
#define chck_ret() return (!__chck_ret);
#define chck_dump(d)\
	do { if (__chck_ret) (d)->dump(); } while(0)
#define chck(expr, ...)	\
	do { __chck_ret |= check((expr), __VA_ARGS__); } while(0)

static bool add_test1(void)
{
	chck_init();

	DependenceGraph d;
	DGNode n1, n2;

	chck(n1.addControlEdge(&n2), "adding C edge claims it is there");
	chck(n2.addDependenceEdge(&n1), "adding D edge claims it is there");

	d.addNode(&n1);
	d.addNode(&n2);

	int n = 0;
	for (auto I = d.begin(), E = d.end(); I != E; ++I) {
		++n;
		chck(*I == &n1 || *I == &n2, "Got some garbage in nodes");
	}

	chck(n == 2, "BUG: adding nodes to graph, got %d instead of 2", n);

	int nn = 0;
	for (auto NI = n1.control_begin(), NE = n1.control_end(); NI != NE; ++NI){
		chck(*NI == &n2, "Got wrong control edge");
		++nn;
	}

	chck(nn == 1, "BUG: adding control edges, has %d instead of 1", nn);

	nn = 0;
	for (auto NI = n2.dependence_begin(), NE = n2.dependence_end(); NI != NE; ++NI) {
		chck(*NI == &n1, "Got wrong control edge");
		++nn;
	}

	chck(nn == 1, "BUG: adding dep edges, has %d instead of 1", nn);

	chck_dump(&d);
	chck_ret();
}

int main(int argc, char *argv[])
{
	bool ret = true;

	ret |= add_test1();

	return ret;
}
