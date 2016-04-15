struct m {
	int a;
};

// don't declare get_ptr function,
// so that analyses have it covered in bitcast
// we have another test that declares it

int main(void)
{
	struct m real;
	struct m *m = get_ptr(&real);
	m->a = 13;

	test_assert(real.a == 13);
	return 0;
}
