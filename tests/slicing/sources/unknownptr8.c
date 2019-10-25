int a = -1;
extern int *foo();
int main(void) {
	a = 0;
	a |= 0x1;
	if (a == 0) {
		a |= 0x4;
	} else {
		a |= 0x2;
	}
	int *x = foo();
	test_assert(*x == 3);
	return 0;
}
