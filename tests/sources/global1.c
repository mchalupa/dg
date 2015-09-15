#include <assert.h>

int a;

void foo()
{
	a = 1;
}

/* basic test */
int main(void)
{
	a = 0;
	foo();

	assert(a == 1);
	return 0;
}
