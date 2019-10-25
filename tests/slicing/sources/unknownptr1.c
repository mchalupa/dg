int *foo(int *a);

int main(void)
{
	int a, b, c;

	int *p = foo(&a);
	// the p points to unknown location, so
	// slicer must not remove *p = 8,
	// since it may point to a
	*p = 8;

	test_assert(a == 8);
	return 0;
}
