int array[5];

void set(int a)
{
	array[3] = a;
}

int main(void)
{
	set(4);
	test_assert(array[3] == 4);
	return 0;
}
