extern int glob;

int *glob_ptr(int *x) {
        (void)x;
        return &glob;
}
