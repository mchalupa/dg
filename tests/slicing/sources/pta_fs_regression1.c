#include <stdio.h>

int foo()
{
	return 1;
}

FILE *get_output(void);

int main(void)
{
	FILE *output = stderr;
	if (foo())
		output = get_output();

	test_assert(output == stdout);
	return 0;
}
