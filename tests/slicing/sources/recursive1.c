void set(int *n, int *x)
{
	if (!x) {
		x = n;
		set(n, x);
	} else
		*x = 13;
}

int main(void)
{
	int a;
	set(&a, 0);

	test_assert(a == 13);
	return 0;
}
