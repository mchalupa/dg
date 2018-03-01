struct callbacks {
	int *(*reta)(int *, int *);
	int *(*retb)(int *, int *);
};

int *foo1(int *a, int *b)
{
	return a;
}

int *foo2(int *a, int *b)
{
	return b;
}

struct callbacks cb = {
	foo1,
	foo2
};

int main(void)
{
	int a, b;
	int *p;
	unsigned long fptr = (unsigned long) cb.retb;
	int *(*f)(int *, int *) = (int *(*)(int *, int *)) fptr;

	p = f(&a, &b);
	*p = 7;

	test_assert(b == 7);
	return 0;
}
