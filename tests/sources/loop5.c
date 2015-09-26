#include <assert.h>

int glob = 1;
int num;
volatile void check(int c)
{
	++num;
}

int main(void)
{
	int a = 0, b = 0;
	while (a < 10) {
		if (a++ > 0)
			// check that we haven't deleted b++ later
			assert(b > 0);
		b++;
	}

	return 0;
}
