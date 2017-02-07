/*
 * The code taken from #135,
 * author: @thierry-tct  <tctthierry@gmail.com>
 */

int x;

void foo(int a) {
        x = 1;
}

void foo2(int a) {
        x = a;
}

struct T {
     void (*f) (int c);
     int c;
};

int main(void) {
        int a = 13;
        struct T tv[] = {{foo, 7}, {foo2, 4}};
        tv[1].f(a);
        test_assert(x == 13);
        return  0;
}
