#include <stdalign.h>

int main(void) {
    int b = 4;
    alignas(alignof(int **)) char array[100];

    _Static_assert(
            4 * sizeof(int **) <= sizeof array,
            "This test requires that 4 * sizeof(int **) <= sizeof array");

    int **p = (int **) array + 3;
    *p = &b;

    p = 0;
    int *q = *((int **) array + 3);
    *q = 3;

    test_assert(b == 3);
    return 0;
}
