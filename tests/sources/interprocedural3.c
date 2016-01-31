

/* check if returning pointer
 * from function works properly */

int *pick(int *a, int *b)
{
	return b;
}

int main(void)
{
	int a, b, c;
	a = 0;
	b = 1;
	c = 3;

	int *p = pick(&a, &b);
	*p = 13;

	test_assert(b == 13);
	return 0;
}
