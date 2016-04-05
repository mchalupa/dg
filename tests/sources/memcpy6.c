#include <string.h>

int main(void)
{
	int a = 0;
	int *array[10];
	int *array2[10];
	int **p = array;
	int **q = array2;

	array[3] = &a;
	memcpy(q, p, sizeof(array));

	*array2[3] = 13;
	test_assert(13 == a);

	return 0;
}
