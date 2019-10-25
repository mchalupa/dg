/* static_assert(sizeof(int) == 4, "This test assumes sizeof(int) == 4"); */

int main(void)
{
	char a[] = "Hello, world";
	int *p = (int *) a + 3;
	*p = 0;

	test_assert(a[2] == 'l');
	return 0;
}
