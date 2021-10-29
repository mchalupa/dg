#include <stdalign.h>

int main(void) {
    int b = 4;
    int i;
    alignas(alignof(int **)) char array[100];

    _Static_assert(
            4 * sizeof(int **) <= sizeof array,
            "This test requires that 4 * sizeof(int **) <= sizeof array");

    int **p = (int **) array + 3;
    *p = &b;

    int *q;
    for (i = 0; i < sizeof(array); ++i) {
        q = *((int **) array + 3);
        if (q == &b)
            *q = 3;
    }

    test_assert(b == 3);
    return 0;
}
