#include <assert.h>

int a;

void foo()
{
	a = 0;
}

int main(void)
{
	a = 1;
	foo();

	return a;
}
