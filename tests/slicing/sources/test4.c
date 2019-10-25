int set(int *a, int b)
{
	*a = b;
	return b;
}

void clear(int *a)
{
	set(a, 0);
}

/* basic test */
int main(void)
{
	int a, b, c;
	a = 0;
	b = 1;
	c = 3;

	a = b + c;
	b = 3;
	c = 5;

	clear(&a);
	set(&a, 11);
	clear(&a);
	set(&a, 12);
	clear(&a);
	set(&a, 13);

	test_assert(a == 13);
	return 0;
}
