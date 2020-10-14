# Control Dependence Analysis

In DG, we implemented several algorithms for the computation of different control dependencies.
These  control dependencies are:

 - Standard (SCD) control dependence (Ferrante et al. [1])
 - Non-termination sensitive control dependence (NTSCD) (Ranangath et al.[2])
 - Decisive order dependence (DOD) (Ranangath et al.[2])
 - [experimental/WIP] Strong control closures (Strong CC) (Danicic et al.[3])
 

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
  
Then there are methods for closure-based algorithms, but these are mostly unimplemented (in fact, the Strong CC algorithm
works, just these getter methods are not implemented yet).

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

Also, NTSCD and DOD algorithms can be executed on interprocedural (inlined) CFG (ICFG).
That is, a one big CFG that contains nodes for all basic blocks/instructions of the
program and there are regular intraprocedural edges and also interprocedural
edges going between calls and entry blocks/instructions and from returns to return-sites.
For this functionality, use -cda-icfg.

## Tools

There is the `llvm-cda-dump` tool that dumps the results of control dependence analysis.
By default, it shows the results directly on LLVM bitcode. You can you the `-ir`switch to see the internal
representation from the analyses. To select the analysis to run, use the `-cda` switch with one of these options:

|opt             | description                           |
|----------------|---------------------------------------|
|`scd`             | standard CD (the default)             |
|`standard`        | an alias for stanard                  |
|`classic`         | an alias for standard                 |
|`ntscd`           | non-termination sensitive CD          |
|`ntscd2`          | NTSCD (a different implementation)    |
|`ntscd-ranganath` | Ranganath et al's algorithm for NTSCD (warning: it is incorrect) |
|`dod`             | Standalone DOD computation            |
|`dod-ranganath`   | Ranganath et al's algorithm (the original algorithm was incorrect, this is a fixed version) |
|`dod+ntscd`       | NTSCD + DOD                           |
|`scc`             | Strong control closures               |


Note that `llvm-slicer` takes the very same options.

There are also tools `llvm-ntscd-dump` specialized for showing internals and results of the NTSCD analysis,
`llvm-cda-bench` that benchmarks a given list of analyses (the list is given without the `-cda` switch,
e.g., `-ntscd -ntscd2 -dod`, see the help message) on a given program, and `llvm-cda-stress`
that works like `llvm-cda-bench` with the difference that it generates and uses a random control flow graph
and it works with only a subset of analyses (all except SCD).

## Other notes

The algorithm for computing standard control dependencies does not have a generic implementation in DG
as we heavily rely on LLVM in computation of post dominators.



[1] Jeanne Ferrante, Karl J. Ottenstein, Joe D. Warren: The Program Dependence Graph and Its Use in Optimization.
    ACM Trans. Program. Lang. Syst. 9(3): 319-349 (1987)


[2] Venkatesh Prasad Ranganath, Torben Amtoft, Anindya Banerjee, Matthew B. Dwyer, John Hatcliff:
    A New Foundation for Control-Dependence and Slicing for Modern Program Structures. ESOP 2005: 77-93
    
[3] Sebastian Danicic, Richard W.Barraclough, Mark Harman, John D.Howroyd, Ákos Kiss, Michael R.Laurence: A unifying theory of control dependence and its application to arbitrary program structures. Theoretical Computer Science, Volume 412, Issue 49, 2011, Pages 6809-6842
