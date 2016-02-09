# DependenceGraph
DependenceGraph is a library which implementats dependence graph for programs.
It contains set of generic templates that can be specialized to user's needs.
DependenceGraph can be used for different analyses, optimizations or program slicing.

Whole project is under hard developement and lacks documentation for now,
so in the case of need, contact us by an e-mail (below).

## LLVM DependenceGraph && llvm-slicer

We have implemented dependence graph for LLVM and a static slicer for LLVM.

### Requirements & Compilation

LLVM DependenceGraph needs LLVM 3.4.
Fully manual configuration would look like this:

```
LLVM_DIR=path/to/llvm/share/llvm/cmake cmake -DLLVM_SRC_PATH=/path/to/src -DLLVM_BUILD_PATH=/path/to/build .
```

LLVM\_DIR is environment variable used by LLVM to find cmake config files
(it points to the build or install directory),
LLVM\_SRC\_DIR is variable that tells cmake where to look for llvm's sources
and it is used to override include paths. The same holds for LLVM\_BUILD\_PATH
that is used for overriding library paths. Usually, you don't need to specify
all these variables. When LLVM 3.4 is installed on your system in standard paths
(and no other LLVM is installed), the configuration should look just like:

```
cmake .
```

If there is any collision (i. e. more versions of LLVM installed),
you may need to define LLVM\_DIR variable to point to the directory where
config files of the installed version are ($PREFIX/share/llvm/cmake).
If you have LLVM compiled from sources, but not installed anywhere,
you may need to use LLVM\_SRC\_PATH and LLVM\_BUILD\_PATH variables too.
For the last case, suppose you have LLVM built in /home/user/llvm-build from
sources in /home/user/llvm-src. Then following configuration should work:

```
LLVM_DIR=/home/user/llvm-build/share/llvm/cmake cmake -DLLVM_SRC_PATH=/home/user/llvm-src -DLLVM_BUILD_PATH=/home/user/llvm-build .
```

After configuring the project, usual make takes place:

```
make -j4
```

### Using the slicer

Compiled slicer can be found in the tools subdirectory. Basic usage is as follows:

```
./llvm-slicer -c slicing_criterion bytecode.bc
```

The slicing_criterion is a call-site of some function or 'ret' when slicing
with respect to return value of the main function.

To export dependence graph to .dot file, use:

```
./llvm-dg-dump bytecode.bc > file.dot
```

You can highligh nodes from dependence graph that will be in the slice using -mark switch:

```
./llvm-dg-dump -mark slicing_criterion bytecode.bc > file.dot
```

For more information you can write e-mails to:
<statica@fi.muni.cz>
<mchqwerty@gmail.com>
