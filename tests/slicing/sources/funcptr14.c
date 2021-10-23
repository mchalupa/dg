#include <stdlib.h>
#include <string.h>

static int mode = 2;

static void *zalloc(size_t x) {
    void *p = malloc(x);
    memset(p, 0, x);
    return p;
}

static void *foo(size_t x) {
    (void) x;
    static int bar = 0xfeed;
    return &bar;
}

static void *foo2(size_t x) {
    (void) x;
    static int bar = 0xbee;
    return &bar;
}

int main(void) {
    void *(*f)(size_t);
    if (mode == 0)
        f = malloc;
    else if (mode == 1)
        f = zalloc;
    else if (mode == 2)
        f = foo;
    else
        f = foo2;

    int *p = f(sizeof(int));

    if (mode == 1) {
        test_assert(*p == 0);
    } else if (mode == 2) {
        test_assert(*p == 0xfeed);
    } else if (mode > 2) {
        test_assert(*p == 0xbee);
    }

    if (mode <= 1)
        free(p);

    return 0;
}
