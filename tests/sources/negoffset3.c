#include <assert.h>

void *shift(int *mem)
{
	return mem + 20;
}

int main(void)
{
	int array[10];
	int *a = shift(array);
	int *p = a - 16;
	*p = 3;
	assert(array[4] == 3);
	return 0;
}
