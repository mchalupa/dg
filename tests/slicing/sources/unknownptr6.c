int a = 3;
extern int *foo();
int main(void) {
	int *x = foo();
	a = 2;
	test_assert(*x == 2);
	return 0;
}
