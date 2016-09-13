void test_assert(int);

void set(unsigned long addr)
{
	*((int *) addr) = 13;
}

int main(void)
{
	unsigned long addr = (unsigned long) malloc(sizeof(int));
	set(addr);
	int *p = (int *) addr;
	test_assert(*p == 13);
}

