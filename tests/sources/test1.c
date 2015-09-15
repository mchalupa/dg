#include <assert.h>

/* basic test */
int main(void)
{
	int a, b, c;
	a = 0;
	b = 1;
	c = 3;

	a = b + c;
	b = 3;
	c = 5;

	assert(a == 4);
	return 0;
}
