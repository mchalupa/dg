#include <string.h>

struct S {
	int *a;
	int b;
};

int main(void)
{
	struct S S1, S2;
	S1.a = &S1.b;
	memcpy(&S2, &S1, sizeof(S1));
	*S2.a = 9;
	test_assert(S1.b == 9);

	return 0;
}
