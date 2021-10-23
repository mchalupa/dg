#include <stdalign.h>

int main(void) {
    int b = 4;
    int i;
    alignas(alignof(int **)) char array[100];

    int **p = (int **) (array + 7 * alignof(int **));
    *p = &b;

    int *q;
    for (i = 0; i < sizeof(array); ++i) {
        q = *((int **) (array + 7 * alignof(int **)));
        if (q == &b)
            *q = 3;
    }

    test_assert(b == 3);
    return 0;
}
