

void set(int *a)
{
	*a = 8;
}

/* globals are initialized by
 * ConstantExpr, so this is different
 * test thatn funcptr1.c */

void (*s)(int *) = set;
int main(void)
{
	int a, b, c;
	a = b + c;
	b = 3;
	c = 5;
	s(&a);

	test_assert(a == 8);
	return 0;
}
