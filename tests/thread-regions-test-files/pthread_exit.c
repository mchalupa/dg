#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>


void *func(void *ptr) {
	int *x = (int *) ptr;
	int *ret = (int *) malloc(sizeof(int));
	int *ret1 = (int *) malloc(sizeof(int));
	*ret1 = 100;
	*x += 1;	
	if (*x == 1) {
		*ret = 42;
		pthread_exit(ret);
	} else if (*x == 2) {
		pthread_exit(ret1);	
	}

    free(ret);
	pthread_exit(NULL);
}

int (*ptr)(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*);

int main() {
	int x = 0;
	int y = 0;
	void *ret_val;

	ptr = &pthread_create;
	pthread_t thread_func;
	
	ptr(&thread_func, NULL, func, &x);
	pthread_join(thread_func, &ret_val);
	free(ret_val);
	return 0;
}
