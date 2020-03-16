struct test_type_t {
    char *type_name;
    int type;
    int (*func_p)(struct test_type_t *tttttttt);
};

extern void test_assert(int);

static int a_func(struct test_type_t *tttttttt) {
    /* The assert should not be sliced away */
    test_assert(1);
    return 1;
}

static struct test_type_t test_instance_2 = {
    .type_name = "instance 2",
    .type = 0,
    .func_p = a_func,
};

int main(int argc, char const *argv[]) {
    struct test_type_t *tt = &test_instance_2;
    tt->func_p(tt);
    return 0;
}
