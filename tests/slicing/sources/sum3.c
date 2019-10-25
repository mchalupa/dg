

#define N 4

int main(void)
{
  int x[N] = {1, 2, 3, 4};
  int temp, i, ret, ret2;

  /* sum elements in array */
  ret = 0;
  for (i = 0; i < N; i++) {
    ret = ret + x[i];
  }

  /* do cyclic shift in array */
  temp=x[0];
  for(i = 0 ; i < N - 1; i++){
     x[i] = x[i + 1];
  }
  x[N - 1] = temp;

  /* sum them again */
  ret2 = 0;
  for (i = 0; i < N; i++) {
    ret2 = ret2 + x[i];
  }

  /* check that the sums equal*/
  test_assert(ret == ret2);
  return 0;
}
