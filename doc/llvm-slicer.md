# llvm-slicer

DG project contains a static slicer for LLVM bitcode. The slicer supports backward and forward (experimental) slicing.

### Using the llvm-slicer

The compiled `llvm-slicer` can be found in the `tools/` subdirectory. First, you need to compile your
program into LLVM IR (make sure you are using the correct version of LLVM binaries if you have more than one):

```
clang -c -emit-llvm source.c -o bitcode.bc
```

If the program is split into more source files (exactly one of them must contain main),
you must compile each of them separately (as above) and then link the bitcodes together using `llvm-link`:

```
llvm-link bitcode1.bc bitcode2.bc ... -o bitcode.bc
```

Now, you're ready to slice the program:

```
./llvm-slicer -c slicing_criterion bitcode.bc
```

The `slicing_criterion` is either a call-site of a function or `ret` to slice
with respect to the return value of the main function. Alternatively, if the program was compiled with the `-g` option,
you can also use `line:variable` as slicing criterion. Slicer will then try to find a use of the variable
on the provided line and mark this use, if found, as a slicing criterion.
If no line is provided (e.g. `:x`), then the variable is considered to be global variable.
You can provide a comma-separated list of slicing criterions, e.g.: `-c crit1,crit2,crit3`.
More about specifying slicing criteria can be found [later](#slicing-criteria) in this document.

You can view the dependence graph that was used to slice the bitcode by exporting it into .dot file.
To achieve this, use `-dump-dg` switch with `llvm-slicer` or a stand-alone tool like
`llvm-dg-dump` (this one is deprecated, but should still work):

```
./llvm-dg-dump bitcode.bc > file.dot
```

You can highlight nodes from the dependence graph that will be in the slice using the `-mark` switch:

```
./llvm-dg-dump -mark slicing_criterion bitcode.bc > file.dot
```

When using `-dump-dg` with `llvm-slicer`, the nodes should be already highlighted.
Also a .dot file with the sliced dependence graph is generated (similar behaviour
can be achieved with `llvm-dg-dump` using the `-slice` switch).

If the dependence graph is too big to be displayed using .dot files, you can debug/see the slice right from
the LLVM language. Just pass `-annotate` option to the `llvm-slicer` and it will store readable annotated LLVM in `file-debug.ll`
(where `file.bc` is the name of file being sliced). There are more options (try `llvm-slicer -help` for show all of them),
but the most interesting is probably the `-annotate slice`:

```
./llvm-slicer -c crit -annotate slice code.bc
```

The content of `code-debug.ll` will look like this:

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

Other options for `-annotate` are `pta`, `dd`, `cd`, `memacc` to annotate points-to information,
data dependencies, control dependencies or memory accessed by instructions.
You can provide comma-separated list of multiple options (`-annotate cd,slice,dd`)

### Example

We can try slicing, for example, this program (with respect to the assertion):

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

Let's say the program is stored in a file `fact.c`. We translate it into LLVM bitcode and then slice it:

```
$ cd tools
$ clang -c -emit-llvm fact.c -o fact.bc
$ ./llvm-slicer -c __assert_fail fact.bc
```

The output is in `fact.sliced`, we can look at the result using `llvm-dis` or `sliced-diff.sh` script:

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

### Slicing criteria (the -c and -2c options)

The `slicing_criterion` is either a call-site of a function or `ret` to slice
with respect to the return value of the main function. Alternatively, if the program was compiled with the `-g` option,
you can also use `line:variable` as slicing criterion. Slicer will then try to find a use of the variable
on the provided line and mark this use, if found, as a slicing criterion. `llvm-slicer` should then inform you
that it matched a slicing criterion with a given instruction.
If no line is provided (e.g. `:x`), then the variable is considered to be a global variable.
You can provide a comma-separated list of slicing criteria, e.g.: `-c crit1,crit2,crit3`.

For example, consider this program:
```C
1. int main() {
2.   int a = 8, b = input();
3.   while (a > b) {
4.     ++b;
5.   }
6.   check(a == b);
7.   check2();
8.   print(a)
}
```
Assuming that the slicing criteria are calls to function `check` (`-c check`),
therefore the slicer will detect the calls to `check` and slice the code w.r.t. these calls
(including their arguments, as the arguments are used by the calls).
Therefore, the slice w.r.t. `-c check` would correspond to (if mapped back to C):

```C
1. int main() {
2.   int a = 8, b = input();
3.   while (a > b) {
4.     ++b;
5.   }
6.   check(a == b);
}
```

The same way you can specify the slicing criteria are calls to `check2`, in which case the slice would be just:
```C
7. check2();
```
as `check2` does not use any variables and therefore has no dependencies
(well, this is not true with non-termination sensitive control dependence).

Alternatively, if you compile the program to LLVM with debugging information (`-g` option),
you can specify a line and variable that should be used as slicing criterion. In our example, if you use `-c 8:a`,
then the program is sliced w.r.t accesses to variable `a` on line 8, so the slice would be:

```C
1. int main() {
2.   int a = 8;
8.  // read of a will stay in LLVM here
}
```
Here is a restriction that the specified variable must be used at the given line.
Just to fill in the details, a slicing criterion is always a node of a dependence graph.
If you dump the dependence graph of the program (`-dump-dg`), then you can see what nodes are there and therefore
what can be a slicing criterion. Alternatively, nodes in dep. graph correspond to instructions,
so a slicing criterion is always an instruction in LLVM (check `-annotate slice` option,
which generates `-debug.ll` file with information about sliced instructions; slicing criteria are marked in the file too).

### Secondary slicing criteria

`llvm-slicer` supports also something that we call a _secondary_ slicing critera. A secondary slicing criterion
is a node (instruction) that is taken as slicing criterion only if it is on a path into a regular slicing criterion.
Take, for example this small program:

```C
int x  = nondet();
assume(x > 0);
check(x > 0);
```

In the example above, if we just set `check` to be the slicing criterion (`-c check`), the `assume` gets sliced away
because it does not modify `x`. Therefore, we can say that calls to `assume` are secondary slicing criteria
(`-2c assume`) and therefore any `assume` that appears on a path into `check` is set as a slicing criterion too
and is preserved.

Secondary slicing criteria does not bring any additional power to slicing. Indeed, we can either say the slicer that
`assume` modifies `x`, or add control dependence from `assume` to nodes reachable from the call (as `assume` may in fact
terminate the execution). However, with secondary slicing criteria, we save edges.

Further, we can specify that a secondary slicing criterion is a _data_ secondary slicing criterion, which means
that it is considered as a slicing criterion only if it is on a path into a regular slicing criterion and
at the same time it uses the same memory as the regular slicing criterion. In `llvm-slicer`, we do that by adding
`()` after the secondary slicing criterion, e.g., `-2c assume()`.

### Slicing criteria (the -sc option)

`llvm-slicer` supports specifying slicing criteria also with `-sc` option, which is designed for further extensions
for C++ and allows to bind a secondary slicing criterion to every primary slicing criterion.
`-sc` takes a list of semi-colon-separated slicing criteria pairs, i.e., 
`-sc 'S;S;S;...'` where `S` describes a primary-secondary slicing criteria pair. A pair is divided by `|`, i.e.
the pair is in the form: `X|Y`. Either the primary or the secondary criterion can be empty (not both, though).
If the primary criterion is empty, the secondary slicing criterion is attached to all primary slicing criteria.

Finally, `X` and `Y` are in the form `file#function#line#obj`.
The fields `file`, `function`, `line`, and `obj` can be empty and the prefix of `###..` can be left out.
The `obj` field matches either a call of a function or a use of a variable (given that the program is
compiled with debugging information) and can be further specified to be a (global) variable or a function call:
`[&][@]obj[()]`. For example, `&x` means variable `x`, `fun()` means call of `fun` and `@g` (or `&@g`) means
global variable `g`.

Examples:

```
'fun()|&x'           -- matches calls of fun() as primary SC and instructions using variable x as secondary SC
'x|f'                -- matches calls of function x or uses of variable x as primary SC and the same with f as secondary SC
'file.c###fun()|&x'  -- matches calls of fun() in file file.c as primary SC and instructions using variable x as secondary SC
'#8#|main#7#'        -- matches all instruction on line 8 (in any file) as primary SC and instructions in (any)
                        function main on line 7 as secondary SC
'foo();boo();|fun()' -- matches calls of functions foo and boo as primary SC and calls of fun as secondary SC for both foo and boo
'foo();boo()|fun()'  -- matches calls of functions foo as primary SC and boo as primary SC with calls of fun as secondary SC
```

Note that the matching is performed in approximation manner, i.e., if the slicer lacks information about an instruction,
it assume it matches the slicing criterion.

## Options

A set of useful options is:

Option             | Arguments        | Description
-------------------|------------------|--------------------------------------------
`-c`               | crit1,crit2,...  | A comma-separated list of slicing criteria
`-2c`              | crit1,crit2,...  | A comma-separated list of secondary slicing criteria
`-annotate`        | val1,val2,...    | Generate annotated bitcode. The argument is a comma-separated list of `slice`,`pta`,`dd`,`cd`,`memacc`
`-allocation-funs` | func:type,...    | Treat the given functions as allocations. `type` is one of `malloc`, `calloc`, `realloc`
`-pta`             | fi, fs, svf       | Set PTA type to flow-insensitive, flow-sensitive, or SVF (if supported)
`-cda`             | standard, ntscd  | Set the type of used control dependencies (termination insensitive or sensitive)
`-interproc-cd`    |                  | Take into account also not returning from function calls (on by default)
`-dump-dg`         |                  | Dump dependence graph to .dot file
`-entry`           | FUN              | Set entry function to FUN
`-forward`         |                  | Perform forward slicing
`-statistics`      |                  | Dump statistics about bitcode before and after slicing
`-undefined-funs`   | {read,write}-{args,any}, pure | Set how to handle calls to undefined functions
`-o`               | FILE             | Output the sliced bitcode into FILE
`-help`            |                  | Show all possible options
