

/* test phi nodes handling */
int main(void)
{
	int a = 2, b = 3, c;
	int *p;

	if (a > b)
		p = &a;
	else
		p = &b;

	*p = 4;

	test_assert(b == 4);
	return 0;
}
