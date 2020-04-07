void test_assert(int);
int *a_ptr(int **x);

void foo(int **x) {
        int *p = a_ptr(x); // returns pointer to a
        *p = 3;
}

int main(void) {
        int a;
        int *b = &a;
        foo(&b);
        test_assert(a == 3);
}

