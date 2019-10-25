#include <string.h>

int main(void)
{
	int a = 0;
	int *array[10];
	int *array2[10];

	array[3] = &a;
	memcpy(array2, array, sizeof(array));

	*array2[3] = 13;
	test_assert(13 == a);

	return 0;
}
