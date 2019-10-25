

int a;

void foo()
{
	a = 1;
}

/* basic test */
int main(void)
{
	a = 0;
	foo();

	test_assert(a == 1);
	return 0;
}
