#include <assert.h>

int glob = 0;
void check_and_set(void)
{
	assert(glob == 0);
	glob = 1;
}

int main(void)
{
	// this assert will pass
	check_and_set();
	// this assert should abort the program
	check_and_set();

	return 0;
}
