#include <stdlib.h>

int glob;

int *glob_ptr(int *);
void test_assert(int);

void foo(int *x) {
        int *p = glob_ptr(x); // returns the pointer to glob
        *p = 3;
}

int main(void) {
        int a = 0;
        foo(&a);
        test_assert(glob == 3);
}

