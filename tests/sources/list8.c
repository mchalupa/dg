#include <stdlib.h>

typedef struct list {
 int key;
 struct list *next;
} mlist;

mlist *head;

void insert_list(int k){
  mlist *l = (mlist*) malloc(sizeof(mlist));
  l->key = k;

  head = l;
}

int main(void){
  mlist *temp;

  insert_list(2);
  test_assert(head->key == 2);
}
