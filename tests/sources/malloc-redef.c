int x;

void *malloc(int i)
{
	if (i == sizeof(int))
		return &x;
	else
		return ((char *) 0);
}

int main(void)
{
	int *p = malloc(sizeof(int));
	if (!p)
		return 1;

	*p = 3;
	test_assert(x == 3);
	return 0;
}

/*
int main(void) {
	unsigned n = 0;
	scanf("%d", &n);
	int i = 0;
	int sum = 0;
	while (++i < n - 3) {}
	sum += i;
	write_sum(sum);
}
*/
