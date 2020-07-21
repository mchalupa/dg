# DG library

DG is a C++ library containing bits for building various static program
analyses.  The main part of the library aims at building dependence graph and
program slicing. In particular, DG contains an implementation of static program
slicer for LLVM bitcode.

#### Supported operating systems

We develop and target DG on GNU/Linux. The project should be compilable on OSX,
though there were some issues in the past (issue [#230](https://github.com/mchalupa/dg/issues/230)).
There were also some unresolved attempts to compile DG on Windows.
Although the code does not use any compiler-specific
or non-portable features, so it should be compilable on Windows,
the compilation was failing due to problems with linking to LLVM libraries
(issues [#196](https://github.com/mchalupa/dg/issues/196) and
[#315](https://github.com/mchalupa/dg/issues/315)).

## Analyses
 - [Pointer analysis](PTA.md)
 - [Data dependence analysis](DDA.md)
 - [Control dependence analysis](CDA.md)
 - [Value-relations analysis](VRA.md)

## Tools
 - [llvm-slicer](llvm-slicer.md)
 - [Other tools](tools.md)

## External Libraries
 - [SVF](SVF.md)
