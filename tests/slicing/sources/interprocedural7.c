

void foo(int *a)
{
	++a;
	*a = 8;
}
int main(void)
{
	int a[2] = {0,1};
	foo(a);
	test_assert(a[1] == 8);
	return 0;
}
