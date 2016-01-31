
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

	// inlined wl_list_init
	list.prev = &list;
	list.next = &list;

	// inlined wl_list_insert
	i1->link.prev = &list;
	i1->link.next = list.next;
	list.next = &i1->link;
	i1->link.next->prev = &i1->link;

	// inlined part of wl_list_remove
	i1->link.prev->next = i1->link.next;
	i1->link.next->prev = i1->link.prev;

	test_assert(list.next == list.prev);
	return 0;
}
