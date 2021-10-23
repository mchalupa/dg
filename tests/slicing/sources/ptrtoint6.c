#include <stdint.h>

void test_assert(int);

void set(uintptr_t addr) { *((int *) addr) = 13; }

int main(void) {
    int a = 0, b = 0;
    set((uintptr_t) &a);
    test_assert(a == 13);
}
