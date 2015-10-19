#include <assert.h>

int main(void)
{
	int array[10];
	int *a = array + 10;
	int *p = a - 10;
	*p = 7;
	assert(array[0] == 7);
	return 0;
}
