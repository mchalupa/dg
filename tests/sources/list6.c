
#include <stdlib.h>

struct wl_list {
	struct wl_list *prev;
	struct wl_list *next;
};

struct item {
	int number;
	struct wl_list link;
};

int main(void)
{
	struct wl_list list;
	struct item *i1 = malloc(sizeof *i1);
	i1->link.next = &i1->link;
	i1->link.next->prev = &list;
	i1->link.next->prev->next = &list;
	i1->link.next->prev->next->prev = NULL;

	test_assert(list.prev == NULL);
	return 0;
}
