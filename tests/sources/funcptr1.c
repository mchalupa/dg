

void set(int *a)
{
	*a = 8;
}

/* basic test */
int main(void)
{
	void (*s)(int *) = set;
	int a, b, c;
	a = b + c;
	b = 3;
	c = 5;
	s(&a);

	test_assert(a == 8);
	return 0;
}
