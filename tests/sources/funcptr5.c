

int *set2(int *a, int *b)
{
	return a;
}

int *set(int *a)
{
	int *(*f)(int *, int *);
	f = set2;
	return f(a, a);
}

int main(void)
{
	int a, b, c;
	int *(*s)(int *) = set;
	a = b + c;
	b = 3;
	c = 5;
	int *p = s(&a);
	*p = 13;

	test_assert(a == 13);
	return 0;
}
