
#include <stdarg.h>

void setv(int num, ...)
{
	va_list l;
	int *x;

	(void) num;

	/* in this test we have only one var arg */
	va_start(l, num);
	x = va_arg(l, int *);
	*x = 13;
	va_end(l);
}

int main(void)
{
	int a;
	setv(1, &a);
	test_assert(a == 13);
	return 0;
}
