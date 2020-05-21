## Tools

In the `tools/` directory, there are a few scripts for convenient manipulation
with sliced bitecode. First is a `sliced-diff.sh`. This script takes file and shows
differences after slicing. It uses `meld` or `kompare` or just `diff` program
to display the results (the first that is available on the system, in this order)

```
./llvm-slicer -c crit code.bc
./slicer-diff.sh code.bc
```

If the program was compiled with `-g`, you can use `llvm-to-source sliced-bitcode.bc source.c` to see the original lines of the source that stayed in the sliced program. Note that this program just dumps the lines of the original code that are present in the sliced bitcode, it does not produce a syntactically valid C program.

Another script is a wrapper around the `llvm-dg-dump`. It uses `xdot` or `evince` or `okular` (or `xdg-open`).
It takes exactly the same arguments as the `llvm-dg-dump`:

```
./llvmdg-show -mark crit code.bc
```
