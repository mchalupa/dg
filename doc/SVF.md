# Integration of SVF into DG

DG can use pointer anlysis from the SVF project.  This analysis should scale
much more better than ours, however, our analysis can be more precise in some
cases.


## Building DG with SVF

To use SVF with DG, you first needs a build of
[SVF](https://github.com/SVF-tools/SVF).  Build the project according its
instructions.  To configure DG with SVF, setup `SVF_DIR` variable to point
to the build of SVF:

```
cmake . -DSVF_DIR=/path/to/svf/build
```

During configuration, cmake should inform you about the found SVF build:

```cmake
-- SVF dir: /home/user/SVF/Debug-build
-- SVF libraries dir: /home/user/SVF/Debug-build/lib
-- SVF include dir: /home/user/SVF/Debug-build/../include
-- SVF libs: LLVMSvf;LLVMCudd
```

In cases of out-of-source build of SVF you may want to manually specify
the varable for include directory:

```
cmake . -DSVF_DIR=/path/to/svf/build -DSVF_INCLUDE=/path/to/svf/include
```

`SVF_DIR` must point to a directory that contains the `lib` subdirectory with
libraries. `SVF_INCLUDE` must point to directly to a directory that contains
the header files.

## Using DG with SVF

DG now integrates only pointer analysis from SVF. To use SVF from C++ API,
just specify in the `LLVMPointerAnalysisOptions` object
that the analysis is of type `LLVMPointerAnalysisOptions::AnalysisType::svf`.

For tools, the switch that turns on SVF pointer analysis is `-pta svf`,
for example:

```
tools/llvm-slicer -c foo -pta svf test.ll
```
