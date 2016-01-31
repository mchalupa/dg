int glob = 0;

void check_and_set(void)
{
	test_assert(glob == 0);
	glob = 1;
}

int main(void)
{
	// this test_assert will pass
	check_and_set();

	// this test_assert should abort the program
	check_and_set();

	return 0;
}
