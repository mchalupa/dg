#include <stdalign.h>

_Static_assert(sizeof(int) == 4, "This test assumes sizeof(int) == 4");

int main(void) {
    alignas(alignof(int)) char a[] = "Hello, world";
    int *p = (int *) a;
    *p = 0;

    test_assert(a[3] == 0);
    return 0;
}
