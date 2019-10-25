

void add2(int *b)
{
	*b +=1;
	*b +=1;
}
void set(int *a)
{
	*a = 8;
	add2(a);
}

/* basic test */
int main(void)
{
	int a, b, c;
	a = 0;
	b = 1;
	c = 3;

	a = b + c;
	b = 3;
	c = 5;
	set(&a);

	test_assert(a == 10);
	return 0;
}
