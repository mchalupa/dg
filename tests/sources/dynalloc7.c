#include <stdlib.h>

int *foo(void)
{
	int *i = malloc(sizeof *i);
	return i;
}

int main(void)
{
	int *a = foo();
	*a = 3;
	int *b = foo();
	*b = 13;
	test_assert(*a == 3);

	return 0;
}
