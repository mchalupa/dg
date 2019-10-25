

struct item {
	int a;
};

struct item *alloc()
{
	struct item *i = malloc(sizeof *i);
	i->a = 13;

	return i;
}

int main(void)
{
	struct item *i = alloc();
	test_assert(i->a == 13);
	return 0;
}
