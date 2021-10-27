#include <stdalign.h>

int main(void) {
    alignas(alignof(int)) char a[] = "Hello, world";

    _Static_assert(sizeof(int) <= sizeof a,
                   "This test assumes that sizeof(int) <= sizeof a");

    int *p = (int *) a;
    *p = 0;

    test_assert(a[3] == 0);
    return 0;
}
