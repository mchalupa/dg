

volatile int getidx(void)
{
	return 2;
}

int main(void)
{
	int array[5];
	int idx = getidx();
	// getidx() returns 2, but slicer
	// does not know it, so array[idx] is
	// pointer with unknown offset to array
	array[idx] = 7;

	test_assert(array[2] == 7);
	return 0;
}
