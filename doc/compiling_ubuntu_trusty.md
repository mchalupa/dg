# The experience with compiling DG on Ubuntu Trusty

This is my experience with compiling DG on (a clean installation of) Ubuntu Trusty for different versions of LLVM.
To summarize, compiling DG with a custom build of LLVM goes fine on Ubuntu Trusty.
Using system LLVM packages does not work at all. The only way how to compile DG without compiling also LLVM
is to use our packages that we use in CI.

If LLVM is compiled from sources, it should be enough to set `LLVM_DIR` (and possibly
`LLVM_SRC_DIR` and `LLVM_BUILD_DIR` to the right directories.

The LLVM 3.4-3.8 packages from the system repositories unfortunately contain broken cmake files.
Not only the config file is named `LLVM-Config.cmake` instead of `LLVMConfig.cmake`
for some LLVM version,but there is also broken the mapping to LLVM libraries.

The configuration with system LLVM 3.9 goes smoothly, but the compilation fails with this error:

```
/usr/lib/llvm-3.9/lib/libLLVMCore.a(AsmWriter.cpp.o): unrecognized relocation (0x2a) in section `.text._ZN4llvm24AssemblyAnnotationWriterD2Ev'

```

Greater versions of LLVM are not available from the repositories.

## Building with LLVM package from our CI

If you do not want to compile LLVM from sources, 
for LLVM 3.8, 4, and 5 you can use the packages that we use in our CI:


```
git clone --depth 1 https://github.com/tomsik68/travis-llvm.git
cd travis-llvm
chmod +x travis-llvm.sh
./travis-llvm.sh 3.8  # change 3.8 to 4 or 5 if wanted
```

Now we can proceed normally with configuration and compilation:

```
cd dg
cmake . -DLLVM_DIR=/usr/share/llvm-3.8/cmake
make -j4
```
