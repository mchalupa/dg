struct m {
	int a;
};

struct m *get_ptr(struct m *);

int main(void)
{
	struct m real;
	struct m *m = get_ptr(&real);
	m->a = 13;

	test_assert(real.a == 13);
	return 0;
}
