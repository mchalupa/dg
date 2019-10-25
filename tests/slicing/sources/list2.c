
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
	wl_list_init(&list);

	struct item *i1 = malloc(sizeof *i1);
	wl_list_insert(&list, &i1->link);
	i1->number = 8;

	test_assert(list.next == &i1->link);
	return 0;
}
