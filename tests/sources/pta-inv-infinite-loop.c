#include <stddef.h>
#include <stdlib.h>

struct list_head {
        struct list_head *next, *prev;
};

struct s {
    struct list_head h0;
    char *b;
    struct list_head h3;
};

int main()
{
    struct s s;
    struct list_head *h3 = &s.h3;
    struct s *ps = (struct s *) ((char *)h3 - offsetof(struct s, h3));

    // let us slice away the error below
    // (we are testing only the pointer analysis now)
    test_assert(1);

    if (ps != &s)
        free(ps);

    ps->b = 0;
    return 0;
}
