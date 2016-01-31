
#include <stdlib.h>

void error(void)
{
	abort();
}

void set(int *a)
{
	// make error kill the program
	// this way we'll know the abort wasn't sliced away
	if (*a == 8)
		error();
}

/* basic test */
int main(void)
{
	int a, b, c;
	a = 8;
	b = 1;
	c = 3;
	b = 3;
	c = 5;
	set(&a);
	return 0;
}
