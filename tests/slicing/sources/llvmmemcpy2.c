

typedef struct Stuff {
 void (*f1)(int *);
 void (*f2)(int *);
} Stuff;

void foo1(int *a)
{
	*a = 1;
}

void foo2(int *a)
{
	*a = 2;
}

int main()
{
 int a = 0;
 Stuff good = {foo1, foo2};
 Stuff bad;
 bad = good;
 bad.f2(&a);
 test_assert(a == 2);
 return 0;
}
