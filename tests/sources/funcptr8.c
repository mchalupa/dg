int a;
void set(void)
{
	a = 8;
}

void foo(void (*f)())
{
	f();
}

/* test passing function pointers */
int main(void)
{
	foo(set);
	test_assert(a == 8);
	return 0;
}
