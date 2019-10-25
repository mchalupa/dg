struct item {
	int a;
};

struct item *alloc(void)
{
	struct item *i = malloc(sizeof *i);
	i->a = 8;
	return i;
}

struct item *foo(struct item *(*f)(void))
{
	return f();
}

/* test passing function pointers */
int main(void)
{
	struct item *i = foo(alloc);
	test_assert(i->a == 8);
	return 0;
}
