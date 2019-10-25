struct m {
	int a;
};

extern struct m *get_ptr(struct m *);

int main(void)
{
	struct m real;
	struct m *(*f)(struct m *) = get_ptr;
	struct m *m = f(&real);
	m->a = 13;

	test_assert(real.a == 13);
	return 0;
}
