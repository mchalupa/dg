

int sum(int x[3])
{
  long long ret;
  ret = x[0] + x[1] + x[2];
  return ret;
}

/* use concrete offsets, so that
 * we won't have any UNKNOWN_OFFSET */
int main(void)
{
  int x[3] = {1, 2, 3};
  int temp, ret, ret2;

  /* sum elements in array */
  ret = sum(x);

  /* do cyclic shift in array */
  temp=x[0];
  x[0] = x[1];
  x[1] = x[2];
  x[2] = temp;

  /* sum them again */
  ret2 = sum(x);

  /* check that the sums equal*/
  test_assert(ret == ret2);
  return 0;
}
