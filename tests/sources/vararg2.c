#include <stdarg.h>

void setv(int num, ...)
{
	va_list l;
	int *x;

	va_start(l, num);

	for (; num > 0; --num) {
		x = va_arg(l, int *);
		*x = 13;
	}

	va_end(l);
}

int main(void)
{
	int a, b, c;
	setv(3, &a, &b, &c);
	test_assert(a == 13);

	return 0;
}
