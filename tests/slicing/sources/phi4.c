void set1(unsigned long addr)
{
	*((int *)addr) = 1;
}

void set2(unsigned long addr)
{
	*((int *)addr) = 2;
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

	f((unsigned long) &a);

	test_assert(a == 2);
	return 0;
}
