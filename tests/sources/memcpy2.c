#include <string.h>

int main(void)
{
	int a = 0;
	int *array[10];
	int *array2[10];

	array[3] = &a;
	/* copy just the one pointer */
	memcpy(array2 + 3, array + 3, sizeof(int *));

	*array2[3] = 13;
	test_assert(13 == a);

	return 0;
}
