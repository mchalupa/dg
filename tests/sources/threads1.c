#include <pthread.h>

void *foo(void *data) {
	int *x = data;
	*x = 7;
	return x;
}

void *foo2(void *data) {
	int *x = data;
	*x = 8;
	return 0;
}

int main(void) {
	int x = 0;
	void *p;
	pthread_t t1, t2;
	pthread_create(&t1, 0, foo, &x);
	pthread_create(&t2, 0, foo2, &x);

	x = 3;

	pthread_join(t1, &p);
	pthread_join(t2, 0);

	*((int *)p) = 4;

	test_assert(x == 4);
	return 0;
}
