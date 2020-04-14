int glob;
int *setglob(void)
{
	glob = 23;
	return &glob;
}

int *foo(int *(f)(void))
{
	return f();
}

int main(void)
{
	int *p = foo(setglob);
	test_assert(*p == 23);
	return 0;
}
