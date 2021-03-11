#!/usr/bin/env python3

from os import listdir
from subprocess import Popen, PIPE, DEVNULL

failed = False

for f in filter(lambda x: x.startswith('llvm'), listdir()):
    print(f, end='')
    p = Popen(['./' + f, '--version'], stdout=DEVNULL, stderr=PIPE)
    out = p.communicate()[1].decode()

    # FIXME: some tools do not accept '--version'
    if p.returncode == 0:
        print("\u001b[32m OK\u001b[0m")
        continue

    failed = True
    print("\u001b[31m NOK\u001b[0m")
    print(out)

exit(failed)
