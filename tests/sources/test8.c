void test_assert(int);

int getVal()
{
	return 9;
}

int main(void)
{
	int a = getVal();
	test_assert(a == 9);

	return 0;
}
