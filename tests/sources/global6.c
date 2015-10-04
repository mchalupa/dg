#include <assert.h>

int a, b, c = 1;

void setB(void)
{
	b = 1;
}

void setA(void)
{
	a = 1;
	setB();
}

int check(void)
{
	// let assert kill the program
	// if everything is allright
	assert(a == 0);
	return a == 1;
}

int main(void)
{
	while(c) {
		if (b == 0)
			goto L;

		if (check()) {
			c = 0;
			break;
L:
			setA();
		}
	}

	return 0;
}
