int main(void)
{
	int a, b, c = 7;

	if (c > 7)
		a = 7;
	else
		a = 8;

	/* this is a regression. Once slicer tried
	 * to remove phi that was created due to this
	 * assignment, it crashed due to dangling reference */
	b = a;
	test_assert(c == 7);

	return 0;
}
