// file ldv-regression/alias_of_return.c_true-unreach-call.i
// from SV-COMP (revision 9113fca)

int *return_self (int *p)
{
	return p;
}

void test_assert(int);

int main()
{
	int a = 1,*q;
	q = return_self(&a);
	*q = 2;

	test_assert(a == 2);
	return 0;
}
