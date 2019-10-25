#include <stdlib.h>

struct s {
	int *a;
	int *b;
};

int main(void)
{
	int a, b;
	struct s *s1, *s2;

	s1 = malloc(sizeof *s1);
	if (!s1)
		return 1;

	s1->a = &a;
	s1->b = &b;

	s2 = realloc(s1, sizeof(*s2));
	if (!s2)
		return 1;

	*s2->a = 9;
	test_assert(a == 9);

	return 0;
}
