
#include <stdlib.h>

struct wl_list;

void wl_list_init(struct wl_list *list);
void wl_list_insert(struct wl_list *list, struct wl_list *elm);
void wl_list_remove(struct wl_list *elm);
int wl_list_length(const struct wl_list *list);
int wl_list_empty(const struct wl_list *list);

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
	list.next = &list;
	list.prev = &list;

	struct item *i1 = malloc(sizeof *i1);

	i1->link.prev = &list;
	i1->link.next = list.next;
	list.next = &i1->link;
	i1->link.next->prev = &i1->link;

	wl_list_remove(&i1->link);

	test_assert(wl_list_empty(&list));
	return 0;
}
