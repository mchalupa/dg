# Pointer Analysis in DG

Pointer analysis (PTA) implementation in DG consists of a general part and LLVM
part.  The general part include a program representation (graph) and the
pointer analysis algorithm that runs on the graph.  The LLVM part takes care of
building the pointer graph from the LLVM module and translating results from
the analysis to consumers of LLVM bitcode (i.e., if we ask for points-to set of
a LLVM value, we get a set of LLVM values and not the analysi's internal
nodes).

The program representation (`PointerGraph`) is a graph whose nodes (`PSNode`)
contain information about pointer handlings, e.g., casts, storing and loading
to/from memory, shifting, etc.  Further, the graph is subdivided into subgraphs
(`PointerSubgraph`) that correspond to procedures.

Once we build the pointer graph, we can run an analysis on it.  In DG, the
pointer analysis class has virtual `getMemoryObjects` method whose definition
changes the behavior of the analysis (along with auxiliary `processefore` and
`processAfter`).  Once the `run` method of pointer analysis is invoked, the
analysis computes points-to sets of nodes (that correspond to top-level values
in LLVM) until fixpoint is reached.

We have implemented flow-sensitive (data-flow) and flow-insensitive
(Andersen's-like) pointer analysis (this one is used by default).

## LLVM pointer analysis

Files from [dg/llvm/PointerAnalysis/](../include/dg/llvm/PointerAnalysis/)

The pointer analysis for LLVM is provided by the `LLVMPointerAnalysis` class.
This class can have different implementations, all of them complying with a
basic public API:

##### `hasPointsTo`

returns true if
  1) PTA has any points-to set for the given value
  2) the points-to set is non-empty```

##### `getLLVMPointsTo`

returns points-to set (object of the class `LLVMPointsToSet`, see below) for the given value.
If `hasPointsTo` would be false for the given value, points-to set containging the only
element `unknown` is returned.

##### `getLLVMPointsToChecked`

returns points-to set (object of the class `LLVMPointsToSet`) for the given value
and a bool which corresponds with the result of `hasPointsTo`.   
The boolean returned is good for checking whether the `unknown` pointer in the points-to set
(if any) is a result of the analysis (e.g., a pointer returned from an undefined function) or
is there because the analysis has no information about the queried value.

##### `getAccessedMemory`

is a wrapper around getLLVMPointsTo that returns `LLVMMemoryRegionSet`.
This object represents a set of tripples (memory, offset, length) describing the regions
of memory accessed by the given instruction (e.g., store or load).


`LLVMPointsToSet` is an object that yields `LLVMPointer` objects upon
iteration.  Each `LLVMPointer` object is a pair of LLVM `Value` and `Offset`
containing the allocation that allocated the pointed memory and the offset into
the memory.  `LLVMPointsToSet` has also methods `hasUnknown`, `hasNull`, and
`hasInvalidated` that return true if the points-to set contains `unknown`,
`null`, or `invalidated` (i.e., pointing to freed or destroyed memory) element.
Note that these elements are not physically present in the points-to set as
there are no LLVM Values that would represent them (and thus could be returned
in LLVMPointer).

## Tools

Results of pointer analysis can be dumped by the `llvm-pta-dump` tool which can be found in `tools/` directory.
Possible options are:

Option                | Values      | Description
----------------------|-------------|-------------
`-pta`                | fi, fs, inv, svf | Type of analysis - flow-insensitive, flow-sensitive,                                     flow-sensitive with tracking invalidated memory, and SVF (if available)
`-pta-field-sensitive` | BYTES       | Set field sensitivity: how many bytes to track on each object
`-callgraph`          |             | Dump also call graph
`-callgraph-only`     |             | Dump only call graph
`-iteration`          | NUM         | How many iterations to perform (for debugging)
`-graph-only`         |             | Do not run PTA, just build and dump the pointer graph
`-stats`              |             | Dump statistics
`-entry`              | FUN         | Set entry function to FUN
`-dbg`                |             | Show debugging messages
`-ir`                 |             | Dump internal representation of the analysis
`-c-lines`            |             | Dump output on the level of C lines (needs debug info)
`-dot`                |             | Dump IR and results of the analysis to .dot file
`-v` `-vv`            |             | Verbose output

Further, there is the tool `llvm-pta-ben` for evaulation of files annotated according to the [PTABen](https://github.com/SVF-tools/PTABen) project, and `llvm-pta-compare` that compares results different pointer analyses.
