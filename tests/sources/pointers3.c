

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
	A[3].p.a = 4;
	A[5].i.ptr = &A[3].p.a;
	/* make the pointer point to unknown offset
	 * for points-to analysis */
	int idx = 3;
	int *p = &A[idx].p.a;

	test_assert(*p == 4);
	return 0;
}
