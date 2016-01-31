void error(void)
{
	/* OK, reached */
	test_assert(1);
}

void set(int *a)
{
	if (*a == 8)
		error();
}

/* basic test */
int main(void)
{
	int a, b, c;
	a = 8;
	b = 1;
	c = 3;
	b = 3;
	c = 5;

	set(&a);

	return 0;
}
