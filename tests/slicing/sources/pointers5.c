#include <string.h>

int main(void)
{
	// enough to keep pointer on 32 and 64 bit
	char bytes[8];
	int a = 3;
	int *p = &a;
	int *q;
	memcpy((void *) bytes, &p, sizeof p);
	memcpy(&q, (void *) bytes, sizeof p);
	*q = 13;

	test_assert(a == 13);
	return 0;
}
