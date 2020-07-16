# The experience with compiling DG on Ubuntu Trusty

This is my experience with compiling DG on (a clean installation of) Ubuntu Xenial for different versions of LLVM.
Compiling DG with LLVM built from sources should work fine, so here I focus only on the compilation
with LLVM from the repositories packages.


First of all, install the dependencies.

```
apt update
apt install git cmake make zlib1g-dev clang g++ python3
```

## LLVM 3.8

The default LLVM is 3.8 which you can install with:

```
apt install llvm
```

Unfortunately, it still suffers from the broken cmake files as in Ubuntu Trusty
(the files are in `/usr/share/llvm-3.8/cmake/`, but cmake is instructed to look
for the configuration files in `/usr/share/llvm/cmake/`. So for LLVm 3.8, the
only option is to build LLVM from sources or use our CI package
(see [Compiling on Ubuntu Trusty](compiling_ubuntu_trusty.md) page).

## LLVM >= 3.9

Compiling DG with system LLVM 3.9 and higher (Xenial has packages
for LLVM 3.9, 4.0, 5.0, 6.0, and 8.0) should work out of the box.
After the installation of prerequisities, just install LLVM, configure DG
and compile (replace `-3.9` with the desired version of LLVM):

```
apt install llvm-3.9
cmake .                # you may need to use -DLLVM_DIR=/usr/lib/llvm-3.9/cmake
make -j2
```

Don't forget to compile source code to LLVM with the right version of clang:

```
apt install clang-3.9
```

The binaries `clang`, `llvm-link`, `opt`, and `lli` (for running slicing tests) may have the suffix `-3.9`,
so you may need to create symbolic links with names without the suffix to successfully run tests.

