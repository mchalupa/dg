int main(void) {
    int *p = malloc(4);
    if (!p)
        return 0;
    for (int i = sizeof(int); i < 10; ++i) {
        p = realloc(p, i);
        if (!p)
            return 0;
        *p = i;
    }
    test_assert(*p == 9);
    free(p);
}
