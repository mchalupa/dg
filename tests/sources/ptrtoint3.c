unsigned long foo(int *a)
{
	return (unsigned long) a;
}

int main(void)
{
	int c;
	int *p = (void *) foo(&c);
	*p = 3;

	test_assert(c == 3);
	return 0;
}
