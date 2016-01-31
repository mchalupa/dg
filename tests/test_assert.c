#include <stdio.h>
#include <assert.h>

void test_assert(int cond)
{
	if (cond) {
		printf("Assertion PASSED\n");
	} else {
		printf("Assertion FAILED\n");
		assert(0 && "assertion failed");
	}

}
