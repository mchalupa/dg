#!/usr/bin/env python3

from os import listdir, access, X_OK
from subprocess import DEVNULL, PIPE, Popen
from shutil import which

failed = False
no_dot = which('dot') is None

def is_exe(f):
    return access(f, X_OK)

for f in filter(lambda x: x.startswith('llvm') and is_exe(x), listdir()):
    # skip wrapper scripts if 'dot' is missing as they depend on it
    if no_dot and f in ('llvmdda-dump', 'llvmdg-show'):
        print(f"{f}\u001b[33m SKIP (needs 'dot')\u001b[0m")
        continue
    print(f, end='')
    p = Popen(['./' + f, '--version'], stdout=DEVNULL, stderr=PIPE)
    out = p.communicate()[1].decode()

    if p.returncode == 0:
        print("\u001b[32m OK\u001b[0m")
        continue

    failed = True
    print("\u001b[31m NOK\u001b[0m")
    print(out)

exit(failed)
