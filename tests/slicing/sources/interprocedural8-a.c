void check_and_set(int *p)
{
	test_assert(*p == 0);
	*p = 1;
}

int main(void)
{
        int *p = malloc(sizeof(int));
        *p = 0;
	// this test_assert will pass
	check_and_set(p);

	// this test_assert should abort the program
	check_and_set(p);

	return 0;
}
