

int glob = 1;
int num;
volatile void check(int c)
{
	++num;

	if (num == 5)
		test_assert(c == 4);
}

int main(void)
{
	int a = 0, b = 0;
	while (a < 10) {
		a += b;
		check(b);
		b++;
	}


	test_assert(num == 5);
	return 0;
}
