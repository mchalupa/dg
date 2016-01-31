

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
	struct item2 I;
	I.p.a = 4;
	I.i.ptr = &I.p.a;

	test_assert(*I.i.ptr == 4);
	return 0;
}
