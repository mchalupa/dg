void foo(unsigned long b)
{
	*((int *) b) = 13;
}

int main(void)
{
	int c;
	foo((unsigned long) &c);
	test_assert(c == 13);
	return 0;
}
