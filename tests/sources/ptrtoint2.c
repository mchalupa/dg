void test_assert(int);

int main(void)
{
	int array[10];
	int *p = &array[0];

	unsigned long ip = ((unsigned long) p);
	ip += sizeof(int);

	int *q = (int *) ip;
	*q = 13;

	test_assert(13 == array[1]);

	return 0;
}
