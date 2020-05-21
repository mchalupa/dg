# DG

[![Build Status](https://travis-ci.org/mchalupa/dg.svg?branch=master)](https://travis-ci.org/mchalupa/dg)

DG is a library containing various bits for program analysis. However, the main motivation of this library is program slicing. The library contains implementation of a pointer analysis, data dependence analysis, control dependence analysis, and an analysis of relations between values in LLVM bitcode. All of the analyses target LLVM bitcode, but most of them are written in a generic way, so they are not dependent on LLVM in particular.

Further, DG contains an implementation of dependence graphs and a [static program slicer](doc/llvm-slicer.md) for LLVM bitcode. Some documentation can be found in the [doc/](doc/) directory.


### Compiling DG

DG needs LLVM 3.4 or higher. The first step is to clone the repository to your machine:

```
git clone https://github.com/mchalupa/dg
cd dg
```

Once you have the project cloned, you need to configure it. When LLVM is installed on your system in standard paths,
the configuration should be as easy as calling `cmake`:

```
cmake .
```
or
```
mkdir build
cd build
cmake ..
```

However, if you have LLVM installed in non-standard paths, or you have several versions of LLVM and want to use a particular one, you must manually specify path to the installation:

```
cmake -DLLVM_SRC_PATH=/path/to/src -DLLVM_BUILD_PATH=/path/to/build -DLLVM_DIR=path/to/llvm/share/llvm/cmake .
```

LLVM\_DIR is an environment variable used by LLVM to find cmake config files
(it points to the build or install directory),
LLVM\_SRC\_DIR is a variable that tells cmake where to look for llvm's sources
and it is used to override include paths. The same holds for LLVM\_BUILD\_PATH
that is used to override library paths. Usually, you don't need to specify
all these variables: LLVM\_DIR variable is useful if there is any collision (i.e. there are more versions of LLVM installed) and you want to use a particular build of LLVM. In that case define the LLVM\_DIR variable to point to the directory where
are the config files of the desired version (`$PREFIX/share/llvm/cmake` or `$PREFIX/lib/cmake/llvm/` for newer versions).
If you have LLVM compiled from sources, but not installed anywhere,
you may need to use LLVM\_SRC\_PATH and LLVM\_BUILD\_PATH variables to specify the directory with sources and build.
As an example, suppose you have LLVM built in /home/user/llvm-build from
sources in /home/user/llvm-src. Then the following configuration should work:

```
cmake -DLLVM_SRC_PATH=/home/user/llvm-src -DLLVM_BUILD_PATH=/home/user/llvm-build -DLLVM_DIR=/home/user/llvm-build/share/llvm/cmake .
```

After configuring the project, usual `make` takes place:

```
make -j4
```

### Testing

You can run tests with `make check` or `make test`. The command runs unit tests and also tests of slicing LLVM bitcode in several different configurations, so it may take a while.


### Using the llvm-slicer

The compiled binary called `llvm-slicer` can be found in the `tools` subdirectory. The basic usage is:

```
./llvm-slicer -c crit code.bc
```

where `crit` is a name of a function whose calls are used as a slicing criteria. Alternatively, if the program was
compiled with `-g`, the you can use "line:var" as slicing criterion, where "var" is a C variable and "line"
is a line of the C code. Slicer than marks as slicing criteria instructions that use "var" at line "line".
More about using slicer can be found in the documentation: [doc/llvm-slicer.md](doc/llvm-slicer.md).

### Tools

The tools subdirectory contains a set of useful programs for debugging
and playing with the llvm bitcode. Except for the `llvm-slicer` you can find there:

* `llvm-dg-dump`      - Dump the dependence graph for given program to graphviz format (to stdout)
* `llvm-pta-dump`     - dump pointer subgraph and results of the points-to analysis to stdout
* `llvm-dda-dump`     - display data dependencies between instructions in a llvm bitcode
* `llvm-cda-dump`     - display control dependencies between instructions in a llvm bitcode
* `llvm-cg-dump`      - dump call graph of the given LLVM bitcode (based on pointer analysis)
* `llvmdg-show`       - wrapper for llvm-dg-dump that displays the dependence graph in dot
* `llvmdda-dump`      - wrapper for llvm-dda-dump that displays data dependencies in dot
* `pta-show`          - wrapper for llvm-pta-dump that prints the PS in grapviz to pdf
* `llvm-to-source`    - find lines from the source code that are in given file
* `dgtool`            - a wrapper around clang that compiles code and passes it to a specified tool

All these programs take as an input llvm bitcode, for example:

```
./pta-show code.bc
```

will show the pointer state subgraph for code.bc and the results of points-to analysis.
Some useful switches for all programs are `-pta fs` and `-pta fi` that switch between flow-sensitive
and flow-insensitive points-to analysis within all these programs that use points-to analysis.

------------------------------------------------

You can find more information about dg in http://is.muni.cz/th/396236/fi_m/thesis.pdf
or you can write e-mails to: <mchqwerty@gmail.com> or to <statica@fi.muni.cz>
