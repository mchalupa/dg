

int main(void)
{
	int a;
	unsigned long iptr = (unsigned long) &a;
	int *p = (int *) iptr;
	*p = 8;
	test_assert(a == 8);

	return 0;
}
