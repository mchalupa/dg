void test_assert(int);
extern int *glob_ptr(int *);
int glob;

void foo2() {
        int x;
        test_assert(*glob_ptr(&x) == 3);
}

int main(void) {
        glob = 3;
        foo2();
	return 0;
}
