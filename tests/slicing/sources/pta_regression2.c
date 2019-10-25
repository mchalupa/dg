int get_int(int);

struct node {
    int value;
};

struct node *nd;

static void insert(int value)
{
    struct node *node = malloc(sizeof *node);
    node->value = value;
    nd = node;
}

static void read()
{
    /* wrapping insert in the loop with
     * no pointers in this function and
     * call to unknown function get_int()
     * resulted in incorrectly built PSS */
    do {
        insert(23);
    } while (get_int(0));
}

int main()
{
    read();
    test_assert(nd->value == 23);

    return 0;
}
