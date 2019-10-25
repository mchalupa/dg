

void set1(int *a)
{
	*a = 1;
}

void set2(int *a)
{
	*a = 2;
}

/* test phi nodes handling */
int main(void)
{
	int a = 2, b = 3, c;
	void (*f)(int *);

	if (a > b)
		f = set1;
	else
		f = set2;

	f(&a);

	test_assert(a == 2);
	return 0;
}
