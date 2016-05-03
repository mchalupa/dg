void foo(unsigned long a, unsigned long b)
{
	*((int **) a) = (int *) b;
}

int main(void)
{
	int c;
	int *p;
	foo((unsigned long) &p, (unsigned long) &c);
	*p = 3;

	test_assert(c == 3);
	return 0;
}
