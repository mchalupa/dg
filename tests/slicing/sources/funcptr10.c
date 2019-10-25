

struct item {
	int a;
};

struct item *alloc(void)
{
	struct item *i = malloc(sizeof *i);
	i->a = 8;
	return i;
}

struct item *alloc2(void)
{
	struct item *i = malloc(sizeof *i);
	i->a = 9;
	return i;
}

struct item *foo(struct item *(*f)(void))
{
	return f();
}

/* test passing function pointers */
int main(void)
{
	int a = 1;
	struct item *i;
	if (a > 0)
		i = foo(alloc2);
	else
		i = foo(alloc);

	test_assert(i->a == 9);
	return 0;
}
