# Data Dependence Analysis

DG contains the analysis of data dependencies (also called value-flow)
based on constructing memory SSA form of the program.

## Public API

The data dependence analysis class for LLVM is `LLVMDataDependenceAnalysis`.
Its constructor takes the LLVM module, results of pointer analysis and optionally an instance of
`LLVMDataDependenceAnalysisOptions`.

Relevant methods from the public API are two polymorphic methods `getLLVMDefinitions`.

One takes LLVM value which is required to read memory (can be checked by `isUse` method)
and returns a vector of LLVM values that may have possibly written the values read
by the given value.

The other takes parameters `where`, `mem`, `off`, and `len` and returns all LLVM values that
may write to the memory allocated by instruction or global variable `mem` at bytes from `off`
to `off + len - 1` and the written value may be read at `where` (i.e., it has not been surely
overwritten at `where` yet).

## Modeling external (undefined) functions

The class `LLVMDataDependenceAnalysisOptions` has the possibility of registering
models for functions. If the analysis then hits a call of the given function,
it uses this model instead of calling the function (if the function is defined
in the module) or instead of assuming that the function may do anything.

The methods that take care of registering models of functions are
`functionModelAddDef` and `functionModelAddUse`.
These methods take the name of the modelled function and a tripple `(argidx, offset, len)`
which means that the function defines/uses the memory pointed by the `argidx`-th argument
(beginning at 0) and define `len` bytes from the memory beginning at `argidx + offset`.
If `len` (`offset`, resp.) are of type `Offset`, these are interpreted as constant numbers.
However, if those are of type `unsigned`, those are interpreted as indexes of arguments
(the same as `argidx`) meaning that the real number of bytes and offset is given
by the argument of the called function. If the argument is not a constant,
it is taken as UNKNOWN. For example,

```C
functionModelAddUse("memset", {0, Offset(0), 2})`
```

tell the analysis that `memset` function defines the memory pointed by the first argument
(with index 0) and this memory is defined from the byte where the first argument points.
Also, only the number of bytes specified by the third argument (with index 2) are used.
For the code

```C
int array[10];
memset(array + 2, 0, 20);
```

The analysis will create a model that tells that this call to `memset` defines bytes 8 - 27 of `array`
(given that the size of int is 4 bytes).Another examples of using these functions can be found in
[LLVMDataDependenceAnalysisOptions.h](../include/dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h).


## Tools

There is `llvm-dda-dump` that dumps the results of data dependence analysis. If dumped to .dot file
(`-dot` option) the computed memory SSA along with def-use chains is shown.
