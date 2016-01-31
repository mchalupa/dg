
#include <stdarg.h>

void setv(int num, ...)
{
	va_list l;
	void (*f)(int *);
	int *x;

	(void) num;

	va_start(l, num);
	f = va_arg(l, void *);
	x = va_arg(l, int *);
	f(x);
	va_end(l);
}

void foo(int *a)
{
	*a = 17;
}

int main(void)
{
	int a = 0;
	setv(1, foo, &a);
	test_assert(a == 17);
	return 0;
}
