/* static_assert(sizeof(int) == 4, "This test assumes sizeof(int) == 4"); */

union BYTE {
	int i;
	char b[4];
};

/* test accessing bytes in int */
int main(void)
{
	union BYTE B;
	B.i = 0;
	B.b[0] = 0xab;
	B.b[1] = 0xab;
	B.b[2] = 0xab;
	B.b[3] = 0xab;
	test_assert(B.i == 0xabababab);

	return 0;
}
