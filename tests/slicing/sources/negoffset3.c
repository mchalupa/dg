

void *shift(int *mem)
{
	return mem + 20;
}

int main(void)
{
	int array[10];
	int *a = shift(array);
	int *p = a - 16;
	*p = 3;
	test_assert(array[4] == 3);
	return 0;
}
