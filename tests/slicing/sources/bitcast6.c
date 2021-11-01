#include <limits.h>

/* test accessing bytes in int */
int main(void) {
    int a = 0;
    char *byte = (char *) &a;

    int result = 0;
    for (unsigned i = 0; i < sizeof(int); i++)
        result |= (byte[i] = 0x1b) << i * CHAR_BIT;

    test_assert(a == result);

    return 0;
}
