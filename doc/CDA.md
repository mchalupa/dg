# Control Dependence Analysis

In DG, we implemented two algorithms for the computation of control dependencies.
The first is the standard (SCD) algorithm due to Ferrante et al. [1] and the other
is an algorithm that computes Non-termination sensitive control dependence (NTSCD) as
defined by Ranangath et al.[2]. However, we do not use their algorithm, but our own
that is described in the master thesis of [Lukáš Tomovič](https://is.muni.cz/th/o1s3u/).

## Public API

The class through which you can run and access the results of control dependence analysis
is called `LLVMControlDependenceAnalysis` and is defined in
[dg/llvm/ControlDependence/ControlDependence.h](../include/dg/llvm/ControlDependence/ControlDependence.h)

The class takes an instance of `LLVMControlDependenceAnalysisOptions` in constructor. This object
describes which analysis to run and whether to compute also interprocedural dependencies (see below).

The public API of `LLVMControlDependenceAnalysis` contains several methods:

* `run()` to run the analysis
* `getDependencies()` to get dependencies of an instruction or a basic block (there are two polymorphic methods).
   As we compute intraprocedural dependencies on basic block level, these two method return different things.
   `getDependencies` for a basic block returns a set of values on which depend all the instructions in the basic
   block. `getDependencies` for instruction then returns additional dependencies, e.g., interprocedural.
   Therefore, if you want _all_ dependencies for an instruction, you should always query both, `getDependencies`
   for the instruction and also `getDependencies` for the basic block of the instruction.
   Note that the return value may be either an instruction or a basic block.   
   If a basic block is returned as a dependence, it means that the queried value depends on the terminator
   instruction of the returned basic block.
   
* `getDependent()` methods return values (instructions and blocks) that depend on the given instruction (block).
   They work similarly as `getDependencies` methods, just return dependent values instead of dependencies.
   If a block is returned, then all instructions of the block depend on the given value.
   
* `getNoReturns()` return possibly no-returning points of the given function (those are usually calls to functions
  that may not return). If interprocedural analysis is disabled, returns always an empty vector.

## Interprocedural dependencies

DG supports the computation of control dependencies that arise due to e.g., calling `abort()` from inside of a procedure.
Consider this example:

```C
void foo(int x) { if (x < 0) abort(); }

int main() {
    int a = input();
    foo();
    assert(a > 0);
}
```

In the example above, the assertion cannot be violated, because for values of `a` that would violate the
assert the program is killed by the call to `abort`. That is, the assertion in fact depends on the if statement
in the `foo` function. Such control dependencies between procedures are omitted by the classical algorithms.
In DG, compute these dependencies by a standalone analysis that runs after computing intraprocedural control dependencies.
Results of the interprocedural analysis are returned by `getDependencies` and `getDependent` along with
results of the intraprocedural analysis (of course, only if interprocedural analysis is enabled by the options
object).

## Tools

There is the `llvm-cda-dump` tool that dumps the results of control dependence analysis.
There is also a tool `llvm-ntscd-dump` specialized for showing internals and results of the NTSCD analysis.

## Other notes

The algorithm for computing standard control dependencies does not have a generic implementation in DG
as we heavily rely on LLVM in computation of post dominators.



[1] Jeanne Ferrante, Karl J. Ottenstein, Joe D. Warren: The Program Dependence Graph and Its Use in Optimization.
    ACM Trans. Program. Lang. Syst. 9(3): 319-349 (1987)


[2] Venkatesh Prasad Ranganath, Torben Amtoft, Anindya Banerjee, Matthew B. Dwyer, John Hatcliff:
    A New Foundation for Control-Dependence and Slicing for Modern Program Structures. ESOP 2005: 77-93
