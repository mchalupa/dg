# DG

[![Build Status](https://travis-ci.org/mchalupa/dg.svg?branch=master)](https://travis-ci.org/mchalupa/dg)

Dg is a library which implements dependence graphs for programs.
It contains a set of generic templates that can be specialized to user's needs.
Dg can be used for different analyses, optimizations or program slicing
(we currently use it for the last one in our tool called Symbiotic:
https://github.com/staticafi/symbiotic). As a part of dg, you can find
pointer analyses, reaching definitions analysis and a static (backward and forward) slicer for LLVM bitcode.

Whole project is under developement and lacks documentation for now,
so in the case of need, contact us by an e-mail (below).

## LLVM DependenceGraph && llvm-slicer

We have implemented a dependence graph for LLVM and a static slicer for LLVM bitcode.

### Requirements & Compilation

LLVM DependenceGraph needs LLVM 3.4 or higher.
Clone the repository to your machine:

```
git clone https://github.com/mchalupa/dg
cd dg
```

Once you have the project cloned, you need to configure it.
Fully manual configuration would look like this:

```
cmake -DLLVM_SRC_PATH=/path/to/src -DLLVM_BUILD_PATH=/path/to/build -DLLVM_DIR=path/to/llvm/share/llvm/cmake .
```

LLVM\_DIR is an environment variable used by LLVM to find cmake config files
(it points to the build or install directory),
LLVM\_SRC\_DIR is a variable that tells cmake where to look for llvm's sources
and it is used to override include paths. The same holds for LLVM\_BUILD\_PATH
that is used to override library paths. Usually, you don't need to specify
all these variables. When LLVM is installed on your system in standard paths,
the configuration should look just like:

```
cmake .
```

If there is any collision (i.e. there are more versions of LLVM installed),
you may need to define the LLVM\_DIR variable to point to the directory where
are the config files of the desired version (`$PREFIX/share/llvm/cmake`
or `$PREFIX/lib/cmake/llvm/` for newer versions).
If you have LLVM compiled from sources, but not installed anywhere,
you may need to use LLVM\_SRC\_PATH and LLVM\_BUILD\_PATH variables too.
For the last case, suppose you have LLVM built in /home/user/llvm-build from
sources in /home/user/llvm-src. Then the following configuration should work:

```
cmake -DLLVM_SRC_PATH=/home/user/llvm-src -DLLVM_BUILD_PATH=/home/user/llvm-build -DLLVM_DIR=/home/user/llvm-build/share/llvm/cmake .
```

After configuring the project, usual make takes place:

```
make -j4
```

### Testing

You can run tests with `make check` or `make test`. To change the pointer analysis used while testing,
you can export `DG_TESTS_PTA` variable before running tests and set it to one of `fi` or `fs`.

### Using the slicer

The ompiled `llvm-slicer` can be found in the `tools` subdirectory. First, you need to compile your
program into LLVM IR (make sure you are using the correct version of LLVM binaries if you have more of them):

```
clang -c -emit-llvm source.c -o bitecode.bc
```

If the program is split into more source files (exactly one of them must contain main),
you must compile all of them separately (as above) and then link the bitcodes together using `llvm-link`:

```
llvm-link bitecode1.bc bitecode2.bc ... -o bitecode.bc
```

Now you're ready to slice the program:

```
./llvm-slicer -c slicing_criterion bitecode.bc
```

The `slicing_criterion` is a call-site of some function or `ret` to slice
with respect to the return value of the main function. Alternatively, if the program was compiled with `-g` option,
you can also use `line:variable` as slicing criterion. Slicer then will try finding a use of the variable on the provided line and marks this use as slicing criterion (if found). If no line is provided (e.g. `:x`), then the variable is considered to be global variable. You can provide a comma-separated list of slicing criterions, e.g.: `-c crit1,crit2,crit3`.

To export the dependence graph to .dot file, use `-dump-dg` switch with `llvm-slicer` or a stand-alone tool
`llvm-dg-dump`:

```
./llvm-dg-dump bitecode.bc > file.dot
```

You can highligh nodes from the dependence graph that will be in the slice using `-mark` switch:

```
./llvm-dg-dump -mark slicing_criterion bitecode.bc > file.dot
```
When using `-dump-dg` with `llvm-slicer`, the nodes should be already highlighted.
Also a .dot file with the sliced dependence graph is generated (similar behviour
can be achieved with `llvm-dg-dump` using the `-slice` switch).

In the `tools/` directory, there are few scripts for convenient manipulation
with sliced bitecode. First is a `sliced-diff.sh`. This script takes file and shows
differences after slicing. It uses `meld` or `kompare` or just `diff` program
to display the results (the first that is available on the system, in this order)

```
./llvm-slicer -c crit code.bc
./slicer-diff.sh code.bc
```
If the program was compiled with `-g`, you can use `llvm-to-source sliced-bitcode.bc source.c` to see the original lines of the source that stayed in the sliced program.

Another script is a wrapper around the `llvm-dg-dump`. It uses `xdot` or `evince` or `okular` (or `xdg-open`).
It takes exactly the same arguments as the `llvm-dg-dump`:

```
./llvmdg-show -mark crit code.bc
```

If the dependence graph is too big to be displayed using .dot files, you can debug the slice right from
the LLVM. Just pass `-annotate` option to the `llvm-slicer` and it will store readable annotated LLVM in `file-debug.ll`
(where `file.bc` is the name of file being sliced). There are more options (try `llvm-slicer -help` for all of them),
but the most interesting is probably the `-annotate slicer`:

```
./llvm-slicer -c crit -annotate slicer code.bc
```

The content of code-debug.ll will look like this:

```LLVM
; <label>:25                                      ; preds = %20
  ; x   call void @llvm.dbg.value(metadata !{i32* %i}, i64 0, metadata !151), !dbg !164
  %26 = load i32* %i, align 4, !dbg !164
  %27 = add nsw i32 %26, 1, !dbg !164
  ; x   call void @llvm.dbg.value(metadata !{i32 %27}, i64 0, metadata !151), !dbg !164
  store i32 %27, i32* %i, align 4, !dbg !164
  ; x   call void @llvm.dbg.value(metadata !{i32* %j}, i64 0, metadata !153), !dbg !161
  ; x   %28 = load i32* %j, align 4, !dbg !161
  ; x   %29 = add nsw i32 %28, 1, !dbg !161
  ; x   call void @llvm.dbg.value(metadata !{i32 %29}, i64 0, metadata !153), !dbg !161
  ; x   br label %20, !dbg !165

.critedge:                                        ; preds = %20
  ; x   call void @llvm.dbg.value(metadata !{i32* %j}, i64 0, metadata !153), !dbg !166
  ; x   %30 = load i32* %j, align 4, !dbg !166
  ; x   %31 = icmp sgt i32 %30, 99, !dbg !166
  ; x   br i1 %31, label %19, label %32, !dbg !166
```

Other interesting debugging options are `ptr`, `rd`, `dd`, `cd`, `postdom` to annotate points-to information,
reaching definitions, data dependences, control dependences or post-dominator information.
You can provide comma-separated list of more options (`-annotate cd,slice,dd`)

### Example

We can try slice for example this program (with respect to the assertion):

```C
#include <assert.h>
#include <stdio.h>

long int fact(int x)
{
	long int r = x;
	while (--x >=2)
		r *= x;

	return r;
}

int main(void)
{
	int a, b, c = 7;

	while (scanf("%d", &a) > 0) {
		assert(a > 0);
		printf("fact: %lu\n", fact(a));
	}

	return 0;
}
```

Let's say the program is stored in a file `fact.c`. We translate it into LLVM bitcode and then slice:

```
$ cd tools
$ clang -c -emit-llvm fact.c -o fact.bc
$ ./llvm-slicer -c __assert_fail fact.bc
```

The output is in fact.sliced, we can look at the result using `llvm-dis` or `sliced-diff.sh` script:

```LLVM
; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %a = alloca i32, align 4
  br label %1

; <label>:1                                       ; preds = %4, %0
  %2 = call i32 (i8*, ...) @__isoc99_scanf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str, i32 0, i32 0), i32* %a)
  %3 = icmp sgt i32 %2, 0
  br i1 %3, label %4, label %safe_return

; <label>:4                                       ; preds = %1
  %5 = load i32, i32* %a, align 4
  %6 = icmp sgt i32 %5, 0
  br i1 %6, label %1, label %7

; <label>:7                                       ; preds = %4
  call void @__assert_fail(i8* getelementptr inbounds ([6 x i8], [6 x i8]* @.str1, i32 0, i32 0), ... [truncated])
  unreachable

safe_return:                                      ; preds = %1
  ret i32 0
}

```

To get this output conveniently, you can use:

```
./sliced-diff.sh fact.bc
```

### Tools

The tools subdirectory contains a set of useful programs for debugging
and playing with the llvm bitcode. Except for the `llvm-slicer` you can find there:

* `llvm-dg-dump`      - Dump the dependence graph for given program to graphviz format (to stdout)
* `llvm-pta-dump`     - dump pointer subgraph and results of the points-to analysis to stdout
* `llvm-dda-dump`     - display reaching definitions in llvm-bitcode
* `llvmdg-show`       - wrapper for llvm-dg-dump that displays the dependence graph in dot
* `llvmdda-dump`      - wrapper for llvm-dda-dump that displays reaching definitions in dot
* `pta-show`          - wrapper for llvm-pta-dump that prints the PS in grapviz to pdf
* `llvm-to-source`    - find lines from the source code that are in given file

All these programs take as an input llvm bitcode, for example:

```
./ps-show code.bc
```
will show the pointer state subgraph for code.bc and the results of points-to analysis.
Some useful switches for all programs are `-pta fs` and `-pta fi` that switch between flow-sensitive
and flow-insensitive points-to analysis within all these programs that use points-to analysis.

------------------------------------------------

You can find more information about dg in http://is.muni.cz/th/396236/fi_m/thesis.pdf
or you can write e-mails to: <mchqwerty@gmail.com> or to <statica@fi.muni.cz>
