int glob;

int *setglob(void)
{
	glob = 23;
	return ((void*)0);
}

int *foo(int *(f)(void))
{
	return f();
}

int main(void)
{
	int *p = foo(setglob);
	test_assert(glob == 23);
	return 0;
}
