void test_assert(int);

void set(unsigned long addr)
{
	*((int *) addr) = 13;
}

int main(void)
{
	int a = 0, b = 0;
	set(&a);
	test_assert(a == 13);
}

