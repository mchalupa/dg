

void set(int *a)
{
	*a = 8;
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
	set(&a);

	test_assert(a == 8);
	return 0;
}
