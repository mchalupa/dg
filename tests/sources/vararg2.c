#include <assert.h>
#include <stdarg.h>

void setv(int num, ...)
{
	va_list l;
	int *x;

	va_start(l, num);
	while (num-- >= 0) {
		x = va_arg(l, int *);
		*x = 13;
	}
	va_end(l);
}

int main(void)
{
	int a, b, c;
	setv(3, &a, &b, &c);
	assert(a == 13);
	return 0;
}
