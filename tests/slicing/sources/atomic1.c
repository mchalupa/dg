#include <stdatomic.h>

extern void test_assert(int);

int a[10] = {1, 2};
_Atomic unsigned long p = 0;

int main(void) {
    atomic_exchange(&p, (unsigned long)&a);
    int *pp = (int*) p;
    test_assert(*(pp + 1) == 2);
}
