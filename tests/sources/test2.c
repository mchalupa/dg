#include <assert.h>

/* basic test with pointers */
int main(void)
{
	int a, b, c;
	int *p;
	a = 0;
	p = &b;
	*p = 1;
	p = &c;
	*p = 3;

	a = b + c;
	b = 3;
	c = 5;

	assert(a == 4);
	return 0;
}
