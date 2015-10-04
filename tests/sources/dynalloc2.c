#include <assert.h>

struct item {
	int a;
};

void alloc(struct item **i)
{
	(*i) = malloc(sizeof(struct item));
	(*i)->a = 13;
}

int main(void)
{
	struct item *i;
	alloc(&i);
	assert(i->a == 13);
	return 0;
}
