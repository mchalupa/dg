struct node {
    int val;
    struct node *next;
    struct node *prev;
};

static struct node* alloc_node(void)
{
    struct node *ptr = malloc(sizeof *ptr);
    ptr->val = 13;
    ptr->next = ((void *)0);
    ptr->prev = ((void *)0);
    return ptr;
}
static void chain_node(struct node **ppnode)
{
    struct node *node = alloc_node();
    node->next = *ppnode;
    *ppnode = node;
}
static void create_sll(const struct node **pp1)
{
    chain_node(pp1);
}

int main()
{
    const struct node *p1;
    create_sll(&p1);
    test_assert(p1->val == 13);

    return 0;
}
