int *get_elem_rec(int **array, int idx) {
	if (idx == 0)
		return array;
	else
		return get_elem_rec(array + 1, idx - 1);
}

int main(void) {
	int b = 2;
	int *array[10];
	array[9] = &b;

	int **p = get_elem_rec(array, 9);
	**p = 3;

	test_assert(b == 3);
	return 0;
}
