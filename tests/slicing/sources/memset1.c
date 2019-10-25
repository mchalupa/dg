#include <string.h>

int main() {
    int array[16];
    memset(array, 0, 16 * sizeof(int));

    for( size_t i = 0; i < 16; ++i)
        test_assert( array[i] == 0 );
    return 0;
}
