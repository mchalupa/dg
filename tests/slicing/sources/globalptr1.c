/*
 * The code taken from #135,
 * author: @thierry-tct  <tctthierry@gmail.com>
 */

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
        struct T tv[] = {{foo, 7}, {foo2, 4}};
        tv[0].f(a);
        test_assert(x == 1);
        return  0;
}
