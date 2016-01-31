/* static_assert(sizeof(int) == 4, "This test assumes sizeof(int) == 4"); */

/* test accessing bytes in int */
int main(void)
{
	int a;
	a = 0;
	char *byte = (char *) &a;
	byte[0] = 0xab;
	byte[1] = 0xab;
	byte[2] = 0xab;
	byte[3] = 0xab;
	test_assert(a == 0xabababab);

	return 0;
}
