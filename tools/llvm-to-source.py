#!/usr/bin/python

import os
import sys
import re
from subprocess import Popen, PIPE

srcdir = None

def get_lines(name, source = None):
    """
    Get lines from source code that are present in the given module
    """

    cmd = ['./llvm-to-source', name]
    if source:
        #cmd.append('-lines-only')
        cmd.append(source)
    p = Popen(cmd, cwd = srcdir, stdout=PIPE, stderr=PIPE)
    out, errs = p.communicate()
    if p.poll() != 0:
        sys.stderr.write(errs)
        sys.exit(1)

    assert not out is None
    return frozenset(map(int, out.split()))

def get_html(name, isbc = False):
    """
    Create html output of the source 'name' with
    highlighted lines that are in 'nums'
    """

    if isbc:
        source = '{0}.c'.format(name[:name.rfind('.')])
    else:
        source = name

    outfile = '{0}.html'.format(source)
    cmd = ['source-highlight', '-n', source]
    p = Popen(cmd, stdout=PIPE, stderr=PIPE)

    out, errs = p.communicate()
    if p.poll() != 0:
        sys.stderr.write(errs)
        sys.exit(1)

    return open(outfile, 'r')

if __name__ == "__main__":
    tmp = os.path.realpath(sys.argv[0])
    srcdir = os.path.abspath(os.path.dirname(tmp))

    nums = get_lines(sys.argv[1], sys.argv[2])
    f = None
    if (len(sys.argv) > 2):
        f = get_html(sys.argv[2])
    else:
        f = get_html(sys.argv[1], True)

    r = re.compile('>0*(\d+):')
    for line in f:
        linenum = None
        match = r.search(line)
        if not match:
            continue
        else:
            linenum = int(match.group(1))

        assert not linenum is None
        if linenum in nums:
	    sys.stdout.write('<span style="background-color:#ddffdd">{0}</span>'.format(line))
        else:
            print(line.rstrip())

    sys.stdout.write('\n')
