int sum(int a, int b)
{
    int j;
    if (a) {
        j = a + b;
    } else {
        j = a - b;
    }
    return j;
}

int main(void)
{
    int i = 2;
    int j = 3;
    int a = sum(i, j);
    a++;
    int b = a + 3;
}
