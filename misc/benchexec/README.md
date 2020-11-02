## Benchexec modules for running tools from DG

Copy the python files to `benchexec/benchexec/tools` and then you can
use the tool in the XML spec of benchexec.

### An example

If you setup benchexec, copy `dgtool.py` to `benchexec/benchexec/tools.py` and
put the path to `dgtool` into PATH (`llvm-slicer` too in the case of
out-of-source build) and run:

```
benchexec llvm-slicer-tests.xml
```

then benchexec will benchmark llvm-slicer on the test files from DG.
