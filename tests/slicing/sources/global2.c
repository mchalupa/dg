int a;

void foo()
{
	a = 0;
}

int main(void)
{
	a = 1;
	foo();

	test_assert(a == 0);
	return a;
}
