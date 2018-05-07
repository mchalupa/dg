#include <stdio.h>
#include <assert.h>

void test_assert(int cond)
{
	if (cond) {
		printf("Assertion PASSED\n");
	} else {
		printf("Assertion FAILED\n");
#ifndef ASSERT_NO_ABORT
		assert(0 && "assertion failed");
#endif
	}

}
