/* with LLVM compiled with
 * assertions enabled, we hit an assert
 * while deleting the argc; variable,
 * because the entry block that contains
 * the alloca instruction (the definition)
 * is deleted earlier than the block containing
 * the load (the argc; line) */

void test_assert(int);

int main(int argc, char *argv[]) {
  if (argc == 0) {
  }

  test_assert(1);
  argc;

  return 0;
}
