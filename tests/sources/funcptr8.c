#include <assert.h>

int a;
void set(void)
{
	a = 8;
}

void foo(void (*f)())
{
	f();
}

/* test passing function pointers */
int main(void)
{
	foo(set);
	assert(a == 8);
	return 0;
}
