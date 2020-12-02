# DG

[![Build Status](https://travis-ci.org/mchalupa/dg.svg?branch=master)](https://travis-ci.org/mchalupa/dg)

DG is a library containing various bits for program analysis. However, the main motivation of this library is program slicing. The library contains implementation of a pointer analysis, data dependence analysis, control dependence analysis, and an analysis of relations between values in LLVM bitcode. All of the analyses target LLVM bitcode, but most of them are written in a generic way, so they are not dependent on LLVM in particular.

Further, DG contains an implementation of dependence graphs and a [static program slicer](doc/llvm-slicer.md) for LLVM bitcode. Some documentation can be found in the [doc/](doc/) directory.


* [Downloading DG](doc/downloading.md)
* [Compiling DG](doc/compiling.md)
* [Using llvm-slicer](doc/llvm-slicer.md)
* [Other tools](doc/tools.md)

------------------------------------------------

You can find a high-level description of DG in [DG: a program analysis library](https://doi.org/10.1016/j.simpa.2020.100038) or [DG: Analysis and slicing of LLVM bitcode](https://www.fi.muni.cz/~xchalup4/dg_atva20_preprint.pdf) papers. More detailed information about dg is in the doc/ folder or in my [master thesis](http://is.muni.cz/th/396236/fi_m/thesis.pdf).

You can write e-mails with issues to <mchqwerty@gmail.com> (or file issue in github).
