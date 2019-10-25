int *foo(int *a, int *b)
{
	return a;
}

int *(*f)(int *, int *) = 0;

int main(void)
{
	int a, b;
	int *p;

	f = foo;
	p = f(&a, &b);
	*p = 7;

	test_assert(a == 7);
	return 0;
}
