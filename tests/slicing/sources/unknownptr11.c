int *glob;
extern void test_assert(int);
extern void foo(void);

int main(void) {
        int a = 0;
        glob = &a;
        foo();
        test_assert(a == 2);
}
