void set(int *a)
{
	test_assert(*a == 8);
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
