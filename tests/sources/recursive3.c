int fact(int a) {
    if (a <= 1) return 1;
    return a * fact(a - 1);
}

int main() {
    int f = fact(3);
    test_assert(f == 6);
    return 0;
}
