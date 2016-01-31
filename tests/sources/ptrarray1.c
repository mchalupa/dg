

int main(void)
{
	int array[10];
	int *ptrs[10];
	int i;

	for (i = 0; i < 10; ++i)
		ptrs[i - 1] = &array[i];

	ptrs[0] = &array[9];
	*ptrs[0] = 1;
	array[0] = 2;

	test_assert(array[9] == 1);
	return 0;
}
