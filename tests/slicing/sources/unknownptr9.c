void test_assert(int);

int *glob;
int *ptr();

void foo1() {
        *ptr() = 2;
}

int main(void) {
        int a;
        glob = &a;
        foo1();
        test_assert(a == 2);
}
