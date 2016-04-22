void foo(int **a, unsigned long b)
{
	*a = (int *) b;
}

int main(void)
{
	int c;
	int *p;
	foo(&p, (unsigned long) &c);
	*p = 3;

	test_assert(c == 3);
	return 0;
}
