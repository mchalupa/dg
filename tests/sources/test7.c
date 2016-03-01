void set(int **y, int *x)
{
	*y = x;
}

int main(void)
{
	int a = 1, b = 2, *p;
	if (a > b) {
		set(&p, &b);
		*p = 3;
	} else {
		set(&p, &a);
		*p = 4;
	}

	test_assert(a == 4);

	return 0;
}
