#include <assert.h>

int b;

void setB(void)
{
	b = 1;
}

void setA(void)
{
	setB();
}

int main(void)
{
	setA();
	assert(b == 1);
	return 0;
}
