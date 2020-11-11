## Tools

In the `tools/` directory, there are a few scripts for convenient manipulation
with sliced bitcode. First is a `sliced-diff.sh`. This script takes file and shows
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

### All tools

The tools subdirectory contains a set of useful programs for debugging
and playing with the llvm bitcode. Except for the `llvm-slicer` you can find there:

* `llvm-dg-dump`      - Dump the dependence graph for given program to graphviz format (to stdout)
* `llvm-pta-dump`     - dump pointer subgraph and results of the points-to analysis to stdout
* `llvm-dda-dump`     - display data dependencies between instructions in a llvm bitcode
* `llvm-cda-dump`     - display control dependencies between instructions in a llvm bitcode
* `llvm-cg-dump`      - dump call graph of the given LLVM bitcode (based on pointer analysis)
* `llvmdg-show`       - wrapper for llvm-dg-dump that displays the dependence graph in dot
* `llvmdda-dump`      - wrapper for llvm-dda-dump that displays data dependencies in dot
* `pta-show`          - wrapper for llvm-pta-dump that prints the PS in grapviz to pdf
* `llvm-to-source`    - find lines from the source code that are in given file
* `dgtool`            - a wrapper around clang that compiles code and passes it to a specified tool

All these programs take as an input llvm bitcode, for example:

```
./pta-show code.bc
```

will show the pointer state subgraph for code.bc and the results of points-to analysis.
Some useful switches for all programs are `-pta fs` and `-pta fi` that switch between flow-sensitive
and flow-insensitive points-to analysis within all these programs that use points-to analysis.
Some of these tools support the `-c-lines` switch that dumps the output on the level of
C file (where an instruction is represented as the line:column pair). For this switch to work,
the program must be compiled with debugging information (`-g`).

### dgtool

`dgtool` is a wrapper around clang that compiles given files (C or LLVM bitcode or a mix),
links them together and then calls the program given as an argument on the bitcode.
For example,

```
dgtool llvm-slicer -c __assert_fail -cda ntscd main.c foo.c
```

You can pass arguments to clang if you preceed them with `-Xclang`, the same way, you can pass arguments to
`dgtool` itself when using `-Xdg`, for example:

```
dgtool -Xclang -O3 -Xclang -fno-discard-value-names -Xdg dbg llvm-dda-dump test.c
```
