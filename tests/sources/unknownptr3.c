

volatile int getidx(void)
{
	return 2;
}

int main(void)
{
	int array[5];
	int idx = getidx();

	// some pointer arithmetic
	int *p = array;
	p += idx;
	++p;

	*p = 7;

	test_assert(array[3] == 7);
	return 0;
}
