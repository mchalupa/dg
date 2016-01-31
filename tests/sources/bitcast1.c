/* test accessing bytes in int */
int main(void)
{
	int a;
	a = 0;
	char *byte = (char *) &a;
	int i;
	for (i = 0; i < sizeof(int); ++i) {
		*byte = 0xff;
		byte++;
	}

	test_assert(a == ~((int) 0));
	return 0;
}
