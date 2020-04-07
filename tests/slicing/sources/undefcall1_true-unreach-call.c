#include <string.h>

int main(void) {
        int FORWARD = 1;
	char *a = "baba";
        char *(*f)(const char *s, int c) = strchr;
        if (FORWARD) {
                f = strrchr;
        }

	char *x = f(a, 'b');
	test_assert(x == a + 2);
}

