

int main(void)
{
	int array[10];
	int *ptrs[10];
	int i;

	for (i = 1; i < 11; ++i)
		ptrs[i - 1] = &array[i % 10];

	ptrs[0] = &array[9];
	*ptrs[0] = 1;
	array[0] = 2;

	test_assert(*ptrs[9] == 2);
	return 0;
}
