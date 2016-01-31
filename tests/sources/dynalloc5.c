
#include <stdlib.h>

struct item {
	int a;
};

/* use function pointer */
void *(*memalloc)(unsigned long size) = malloc;
struct item *alloc()
{
	struct item *i = memalloc(sizeof *i);
	i->a = 13;

	return i;
}

int main(void)
{
	struct item *i = alloc();
	test_assert(i->a == 13);
	return 0;
}
