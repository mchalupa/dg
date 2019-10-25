

struct item {
	int a;
};

struct item *alloc()
{
	struct item *i = calloc(4, sizeof *i);
	(i + 2)->a = 13;

	return i;
}

int main(void)
{
	struct item *i = alloc();
	test_assert((i + 2)->a == 13);
	return 0;
}
