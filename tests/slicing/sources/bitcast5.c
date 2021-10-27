#include <stdalign.h>

int main(void) {
    alignas(alignof(int)) char a[] = "Hello, world";

    _Static_assert(2 * sizeof(int) <= sizeof a &&
                           sizeof(int) > 3 * sizeof(char),
                   "This test assumes that 2 * sizeof(int) <= sizeof a && "
                   "sizeof(int) > 3 * sizeof(char)");

    int *p = (int *) a + 1;
    *p = 0;

    test_assert(a[2] == 'l');
    return 0;
}
