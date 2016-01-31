

struct item {
	int a;
};

void alloc(struct item **i)
{
	(*i) = malloc(sizeof(struct item));
	(*i)->a = 13;
}

int main(void)
{
	struct item *i;
	alloc(&i);
	test_assert(i->a == 13);
	return 0;
}
