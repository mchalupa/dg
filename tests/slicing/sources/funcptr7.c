

void set(int *a)
{
	*a = 8;
}

void foo(void (*f)(int *), int *p)
{
	f(p);
}

/* test passing function pointers */
int main(void)
{
	void (*s)(int *) = set;
	int a, b = 0x235, c = 0xbeef;
	a = b + c;
	b = 3;
	c = 5;
	foo(set, &a);

	test_assert(a == 8);
	return 0;
}
