

struct callbacks {
	void (*f1)(int *);
	void (*f2)(int *);
};

void foo(int *a)
{
	*a = 1;
}

void foo2(int *c)
{
	*c = 2;
}

static struct callbacks cb = {
	.f1 = foo,
	.f2 = foo2
};

int main(void)
{
	int a, b;
	cb.f1(&a);
	cb.f2(&b);

	test_assert(a == 1 && b == 2);
	return 0;
}
