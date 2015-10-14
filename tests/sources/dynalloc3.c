#include <assert.h>

struct item {
	int a;
};

struct item *alloc()
{
	int size = sizeof(struct item);
	struct item *i = malloc(size);
	i->a = 13;

	return i;
}

int main(void)
{
	struct item *i = alloc();
	assert(i->a == 13);
	return 0;
}
