/* Written by @thierry-tct <tctthierry@gmail.com>
 * from issue #133
 */

#include <stdlib.h>

int x;

 void foo(int a) {
         x = 1;
 }

 void foo2(int a) {
         x = 2;
 }

 struct T {
      void (*f) (int c);
      int c;
 };

 int main(void) {
         int a = 1;
         struct T *pt = NULL;
         pt = (struct T*) realloc (pt, sizeof(struct T));
         pt[0].f = (a == 1 ? foo : foo2);
         pt[0].f(a);
         test_assert(x == 1);
         return 0;
}
