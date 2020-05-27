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

## Tools

There is `llvm-dda-dump` that dumps the results of data dependence analysis. If dumped to .dot file
(`-dot` option) the computed memory SSA along with def-use chains is shown.
