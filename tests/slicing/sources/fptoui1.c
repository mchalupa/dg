int main(void){
	int a = 42;
	unsigned long pa = (unsigned long) &a;
	double da = (double) pa;
	unsigned long pa1 = (unsigned long) da;

	test_assert(*((int *)pa1) == a);
	return 0;
}
