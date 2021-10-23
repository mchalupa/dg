#include <stdalign.h>

int main(void) {
    int b = 4;
    alignas(alignof(int **)) char array[100];

    int **p = (int **) (array + 7 * alignof(int **));
    *p = &b;

    p = 0;
    int *q = *((int **) (array + 7 * alignof(int **)));
    *q = 3;

    test_assert(b == 3);
    return 0;
}
