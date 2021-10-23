#include <stdint.h>

void set1(uintptr_t addr) { *((int *) addr) = 1; }

void set2(uintptr_t addr) { *((int *) addr) = 2; }

/* test phi nodes handling */
int main(void) {
    int a = 2, b = 3, c;
    void (*f)(uintptr_t);

    if (a > b)
        f = set1;
    else
        f = set2;

    f((uintptr_t) &a);

    test_assert(a == 2);
    return 0;
}
