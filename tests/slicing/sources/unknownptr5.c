struct m {
	int a;
};

struct m *get_ptr(struct m*);

int main(void)
{
	struct m real1, real2;
	struct m *m = get_ptr(&real1);
	struct m *n = get_ptr(&real2);
	real1.a = 0;
	if (real1.a) {
		real1.a = 11;
		real2.a = 12;
	} else {
		m->a = 13;
		n->a = 14;
	}

	test_assert(real1.a == 13 && real2.a == 14);
	return 0;
}
