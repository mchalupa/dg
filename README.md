# DependenceGraph
DependenceGraph is a library which implementats dependence graph for programs.
It contains set of generic templates that can be specialized to user's needs.
DependenceGraph can be used for different analyses, optimizations or program slicing.

Whole project is under hard developement and lacks documentation for now, so in the case of need, contact us by an e-mail (below).

## LLVM DependenceGraph && llvm-slicer
We have implemented dependence graph for LLVM and a static slicer for LLVM.
The slicer can be found in tools/ directory; basic usage is following:

```
./llvm-slicer -c slicing_criterion bytecode.bc
```

The slicing_criterion is a call-site or 'ret' to slice with respect to return value of main function. Current version of slicer is not doing much about slicing control structure, so we recommend drive sliced bytecode through LLVM optimizations using opt program to get nicer slice (we're working on fixing this ;)

To export dependence graph to .dot file, use:
```
./llvm-dg-dump bytecode.bc > file.dot
```

different settings can be passed to llvm-dg-dump to print just subset of edges and similar, try --help switch


For more information you can write e-mails to:
<statica@fi.muni.cz>
<mchqwerty@gmail.com>
