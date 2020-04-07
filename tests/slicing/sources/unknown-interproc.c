extern void test_assert(int);
int *ptr(int*);

void foo(int *x) {
        int *p = ptr(x);
        *p = 3;
}

int main(void) {
        int a = 0;
        foo(&a);
        test_assert(a == 3);
}

