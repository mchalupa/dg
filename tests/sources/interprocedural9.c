int glob = 0;

void check_and_set(int i)
{
	if (glob == 1) {
		test_assert(i == 1);
		glob = 2;
	} else if (glob == 2) {
		glob = 0;
	} else
		glob = 1;
}

int main(void)
{
	int i = 0;
	for (; i < 4; ++i) {
		// the second test_assert should abort the program
		check_and_set(i);
	}

	/* now the glob should be 1, thus this call should
	 * trigger the assert
	check_and_set(1);
	*/

	return 0;
}
