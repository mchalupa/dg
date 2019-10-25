

int a, b, c;

void setA(void)
{
	a = 1;
}

void setB(void)
{
	b = 1;
}

void setC(void)
{
	c = 1;
}

int main(void)
{
	if (b == 0)
		setB();
	if (a == 0)
		setA();

	test_assert(a == 1);
	return 0;
}
