static int a = 0;

void foo()
{
	test_assert(a == 1);
}

int main(void)
{
	a = 1;
	foo();

	return 0;
}
