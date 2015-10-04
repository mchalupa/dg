#include <assert.h>


int *array;
void set(int a)
{
	array[3] = a;
}

/* basic test */
int main(void)
{
	array = calloc(5, sizeof(int));
	set(4);
	assert(array[3] == 4);
	return 0;
}
