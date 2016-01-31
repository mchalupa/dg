

/* basic test with pointers */
int main(void)
{
	int a, b, c;
	int *p;
	a = 0;
	p = &b;
	*p = 1;
	p = &c;
	*p = 3;

	a = b + c;
	b = 3;
	c = 5;
	p = &a;
	*p = 9;

	test_assert(a == 9);
	return 0;
}
