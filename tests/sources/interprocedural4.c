#include <assert.h>

void set(int *a)
{
	// make assert kill the program
	// this way we'll know the assert won't be sliced away
	assert(*a != 8);
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
