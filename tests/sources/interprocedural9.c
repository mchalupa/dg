#include <assert.h>

int glob = 0;
void check_and_set(void)
{
	if (glob == 1)
		assert(glob != 1);
	else if (glob == 2)
		glob = 0;
	else
		glob = 1;
}

int main(void)
{
	int i = 0;
	for (; i < 5; ++i) {
		// the second assert should abort the program
		check_and_set();
	}

	return 0;
}
