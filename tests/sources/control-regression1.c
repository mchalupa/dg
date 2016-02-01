int main(void)
{
	int a = 0xdeadbee;
	int b = 0;
	int c = 1;

	while(c) {
		if (b == 0)
			goto L;

		test_assert(a == 1);
		if (a == 1) {
			c = 0;
L:
			a = 1;
			b = 1;
		}
	}

	return 0;
}
