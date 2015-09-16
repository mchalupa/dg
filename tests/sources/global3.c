#include <assert.h>

static int a = 0;

void foo()
{
	// let the assert kill test
	assert(a != 1);
}

/* basic test */
int main(void)
{
	a = 1;
	foo();
	return 0;
}
