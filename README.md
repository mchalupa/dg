# DependenceGraph
DependenceGraph is a library which implementats dependence graph for programs.
It contains set of generic templates that can be specialized to user's needs.
DependenceGraph can be used for different analyses, optimizations or program slicing.

Whole project is under hard developement and lacks documentation for now,
so in the case of need, contact us by an e-mail (below).

## LLVM DependenceGraph && llvm-slicer

We have implemented dependence graph for LLVM and a static slicer for LLVM.

### Requirements & Compilation

LLVM DependenceGraph needs LLVM 3.4. Actually developed branch is backport-llvm-3.4,
so checkout the project like this:

```
git clone https://github.com/mchalupa/dg -b backport-llvm-3.4
cd dg
```

One you have the project cloned, you need to configure it.
Fully manual configuration would look like this:

```
LLVM_DIR=path/to/llvm/share/llvm/cmake cmake -DLLVM_SRC_PATH=/path/to/src -DLLVM_BUILD_PATH=/path/to/build .
```

LLVM\_DIR is environment variable used by LLVM to find cmake config files
(it points to the build or install directory),
LLVM\_SRC\_DIR is variable that tells cmake where to look for llvm's sources
and it is used to override include paths. The same holds for LLVM\_BUILD\_PATH
that is used for overriding library paths. Usually, you don't need to specify
all these variables. When LLVM 3.4 is installed on your system in standard paths
(and no other LLVM is installed), the configuration should look just like:

```
cmake .
```

If there is any collision (i. e. more versions of LLVM installed),
you may need to define LLVM\_DIR variable to point to the directory where
config files of the installed version are ($PREFIX/share/llvm/cmake).
If you have LLVM compiled from sources, but not installed anywhere,
you may need to use LLVM\_SRC\_PATH and LLVM\_BUILD\_PATH variables too.
For the last case, suppose you have LLVM built in /home/user/llvm-build from
sources in /home/user/llvm-src. Then following configuration should work:

```
LLVM_DIR=/home/user/llvm-build/share/llvm/cmake cmake -DLLVM_SRC_PATH=/home/user/llvm-src -DLLVM_BUILD_PATH=/home/user/llvm-build .
```

After configuring the project, usual make takes place:

```
make -j4
```

### Using the slicer

Compiled slicer can be found in the tools subdirectory. First, you need to compile your
program into LLVM (make sure you are using the LLVM 3.4 version binaries!):

```
clang -c -emit-llvm source.c -o bytecode.bc
```

If the program is split into more programs (exactly one of them must contain main),
you must compile all of them separately (as above) and then link the bytecodes using llvm-link:

```
llvm-link bytecode1.bc bytecode2.bc ... -o bytecode.bc
```

Now you're ready to slice the program:

```
./llvm-slicer -c slicing_criterion bytecode.bc
```

The slicing\_criterion is a call-site of some function or 'ret' when slicing
with respect to return value of the main function.

To export dependence graph to .dot file, use:

```
./llvm-dg-dump bytecode.bc > file.dot
```

You can highligh nodes from dependence graph that will be in the slice using -mark switch:

```
./llvm-dg-dump -mark slicing_criterion bytecode.bc > file.dot
```

In the tools/ directory, there are few scripts for convenient manipulation
with the sliced bytecode. First is sliced-diff.sh. This script takes file and shows
differences after slicing. It uses meld or kompare or just diff program
to display the results (the first that is available on the system, in this order)

```
./llvm-slicer -c crit code.bc
./slicer-diff.sh code.bc
```

The other is wrapper around llvm-dg-dump. It uses evince or okular (or xdg-open).
It takes exactly the same arguments as llvm-dg-dump:

```
./ldg-show.sh -mark crit code.bc
```

### Example

We can try slice for example this program (with respect to the assertion):

```
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

Let's say the program is stored in file fact.c. So we translate it into LLVM and then slice:

```
$ cd tools
$ clang -c -emit-llvm fact.c -o fact.bc
$ ./llvm-slicer -c __assert_fail fact.bc
```

The output is in fact.sliced, we can look at the result using llvm-dis or using sliced-diff.sh script:

```
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

------------------------------------------------

For more information you can write e-mails to:
<statica@fi.muni.cz>
<mchqwerty@gmail.com>
