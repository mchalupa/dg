static int a;

void foo(int x) {
	if (x > 0) {
		foo(x-1);
	} else {
		a = 5;
	}
}

int main(void) {
	foo(10);
	test_assert(a == 5);
}
