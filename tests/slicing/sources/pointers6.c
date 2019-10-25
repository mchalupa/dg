int main(void) {
	int b = 4;
	char array[100];

	int **p = (int **) (array + 7);
	*p = &b;

	p = 0;
	int *q = *((int **)(array + 7));
	*q = 3;

	test_assert(b == 3);
	return 0;
}
