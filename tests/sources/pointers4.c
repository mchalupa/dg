

struct item {
	int a;
	int *ptr;
};

struct item2 {
	struct item i;
	struct item p;
};

int main(void)
{
	int n = 10;
	struct item2 *A = malloc(n * sizeof(struct item2));
	struct item *i = &A[3].i;
	i->ptr = &i->a;
	*i->ptr = 8;

	struct item2 *ptr = A;
	for (n = 0; n < 4; ++n)
		++ptr;

	test_assert(ptr->i.a == 8);
	return 0;
}
