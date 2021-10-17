#include <assert.h>
#include <stdio.h>

void test_assert(int cond) {
    if (cond) {
        printf("Assertion PASSED\n");
    } else {
        printf("Assertion FAILED\n");
#ifndef ASSERT_NO_ABORT
        assert(0 && "assertion failed");
#endif
    }
}
