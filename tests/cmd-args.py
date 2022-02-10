#!/usr/bin/env python3

from os import listdir, access, X_OK
from subprocess import DEVNULL, PIPE, Popen

failed = False

def is_exe(f):
    return access(f, X_OK)

for f in filter(lambda x: x.startswith('llvm') and is_exe(x), listdir()):
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
