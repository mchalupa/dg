#include <string.h>

#define FORWARD 1

int main(void) {
	char *a = "baba";
        char *(*f)(const char *s, int c) = strchr;
        if (FORWARD) {
                f = strrchr;
        }

	char *x = f(a, 'b');
	test_assert(x == a);
}

