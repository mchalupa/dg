#include <string.h>

int main(void)
{
	int a = 0;
	int *array[10];
	int *array2[10];

	array[3] = &a;
	/* copy just the one pointer, but
	 * shift the memory by 3 elemnts,
	 * thus array2[0] will contain memory
	 * from array[3] */
	memcpy(array2, array + 3, sizeof(int *));

	*array2[0] = 13;
	test_assert(13 == a);

	return 0;
}
