#include <assert.h>

typedef struct Stuff {
 int a;
 int b;
} Stuff;

int main()
{
 Stuff good = {1 ,2};
 Stuff bad;
 bad = good;
 assert(bad.b == 2);
 return 0;
}
