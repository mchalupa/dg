# DG

[![Build Status](https://travis-ci.org/mchalupa/dg.svg?branch=master)](https://travis-ci.org/mchalupa/dg)

DG is a library containing various bits for program analysis. However, the main motivation of this library is program slicing. The library contains implementation of a pointer analysis, data dependence analysis, control dependence analysis, and an analysis of relations between values in LLVM bitcode. All of the analyses target LLVM bitcode, but most of them are written in a generic way, so they are not dependent on LLVM in particular.

Further, DG contains an implementation of dependence graphs and a [static program slicer](doc/llvm-slicer.md) for LLVM bitcode. Some documentation can be found in the [doc/](doc/) directory.


* [Downloading DG](doc/downloading.md)
* [Compiling DG](doc/compiling.md)
* [Using llvm-slicer](doc/llvm-slicer.md)
* [Other tools](doc/tools.md)

------------------------------------------------

You can find more information about dg in http://is.muni.cz/th/396236/fi_m/thesis.pdf
or you can write e-mails to: <mchqwerty@gmail.com> or to <statica@fi.muni.cz>
