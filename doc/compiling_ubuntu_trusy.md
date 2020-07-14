# The experience of compiling on Ubuntu Trusty

This is my experience with compiling DG on (a clean installation of) Ubuntu Trusty for different versions of LLVM.
First of all, we need to compile the dependencies and clone dg:

```
sudo apt update
sudo apt install gcc git cmake make zlib1g-dev clang g++ python3
git clone https://github.com/mchalupa/dg
```

If LLVM is compiled from sources, it should be enough to set `LLVM_DIR` (and possibly
`LLVM_SRC_DIR` and `LLVM_BUILD_DIR` to the right directories.
The rest of this document is concerned with compiling against LLVM from system packages.

## System LLVM 3.4 - 3.8

The LLVM packages from the system repositories unfortunately contain broken cmake files.
Not only the config file is named `LLVM-Config.cmake` instead of `LLVMConfig.cmake`
for some LLVM version,
but there is also broken the mapping to LLVM libraries.

One way how to build DG with LLVM 3.8 is to use the LLVM package that we use in our CI:

```
git clone --depth 1 https://github.com/tomsik68/travis-llvm.git
cd travis-llvm
chmod +x travis-llvm.sh
./travis-llvm.sh 3.8
```

Now we can proceed normally with configuration and compilation:

```
cd dg
cmake . -DLLVM_DIR=/usr/share/llvm-3.8/cmake
make -j4
```

## System LLVM >= 3.9

The system LLVM 3.9 should work without a greater problems.
Greater versions of LLVM are not available from the repositories
(although for LLVM 4 and 5 you can use the packages that we use in our CI, see above)

```
sudo apt install llvm-3.9
cd dg
cmake . -DLLVM_DIR=/usr/share/llvm-3.9/cmake/
make -j4
```
