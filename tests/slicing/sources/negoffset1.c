

int main(void)
{
	int array[10];
	int *a = array + 1;
	int *p = a - 1;
	*p = 0;
	test_assert(array[0] == 0);
	return 0;
}
