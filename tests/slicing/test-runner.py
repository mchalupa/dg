#!/usr/bin/env python3

from sys import stdout, argv
from subprocess import Popen, DEVNULL, PIPE
from os.path import join, dirname, abspath, basename
from os import chdir, getcwd, unlink
from shutil import rmtree

debug = False
have_svf = False

# going to be (possibly) re-set in set_environment()
TOOLSDIR = "../../tools/"
SOURCESDIR = "../sources/"
LLVMCC = "clang"
LLVM_TOOLS_DIR = ""

# RUNDIR=getcwd()


def parse_cmake_cache(cmakecache):
    with open(cmakecache, 'r') as f:
        for line in f:
            if line.startswith('SVF_DIR'):
                have_svf = True
            elif line.startswith('dg_SOURCE_DIR'):
                parts = line.split('=')
                global SOURCESDIR
                SOURCESDIR = abspath(join(parts[1].strip(), 'tests/slicing/sources/'))
            elif line.startswith('dg_BINARY_DIR'):
                parts = line.split('=')
                global TOOLSDIR
                TOOLSDIR = abspath(join(parts[1].strip(), 'tools/'))
            elif line.startswith('LLVMCC'):
                parts = line.split('=')
                global LLVMCC
                LLVMCC = parts[1].strip()
            elif line.startswith('LLVM_TOOLS_DIR'):
                parts = line.split('=')
                global LLVM_TOOLS_DIR
                LLVM_TOOLS_DIR = parts[1].strip()


configs = {
#   '-dda': ['rd', 'ssa'],
    '-pta': ['fi', 'fs', 'inv'],
    '-cd-alg': ['ntscd', 'classic'],
}


def command(cmd):
    if debug:
        print("> " + "  ".join(cmd), flush=True)
        p = Popen(cmd)
    else:
        p = Popen(cmd, stdout=DEVNULL, stderr=DEVNULL)
    return p.wait()


def command_output(cmd):
    if debug:
        print("> " + "  ".join(cmd))
    p = Popen(cmd, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    return out, err, p.poll()


def cleanup():
    oldpath = getcwd()
    chdir('..')
    rmtree(oldpath)


def error(msg):
    from sys import stderr, exit
    print(msg, file=stderr)
    exit(1)


def set_environment():
    try:
        parse_cmake_cache("../../CMakeCache.txt")
    except IOError:
        # assume in-source build where we want to call
        # the test-runner.py from everywhere
        chdir(dirname(argv[0]))


def _getbcname(name):
    if name[-2:] != '.c':
        error('Input is not a .c source code')

    return basename(name[:-2]) + '.bc'


def compile(source, output=None, params=[]):
    if output is None:
        output = _getbcname(source)

    ret = command([LLVMCC, "-include", join(SOURCESDIR, '..', "test_assert.h"),
                   "-emit-llvm", "-c", source, "-o", output] + params)
    if ret != 0:
        error('Failed executing ' + LLVMCC)

    return output


def slice(bccode, args):
    output = bccode + ".sliced"
    slicer = join(TOOLSDIR, "llvm-slicer")

    cmd = [slicer, "-c", "test_assert"] + args + [bccode, "-o", output]
    if command(cmd) != 0:
        error('Failed executing llvm-slicer')

    return output


def link(bccode, codes, output=None):
    if output is None:
        output = bccode + ".linked"

    llvm_link = join(LLVM_TOOLS_DIR, "llvm-link")
    cmd = [llvm_link, bccode, "-o", output] + codes

    if command(cmd) != 0:
        error('Failed executing ' + llvm-link)

    return output


def opt(bccode, passes, output=None):
    if output is None:
        output = bccode + ".opt"

    opt = join(LLVM_TOOLS_DIR, "opt")
    cmd = [opt, bccode, "-o", output] + passes

    if command(cmd) != 0:
        error('Failed executing ' + opt)

    return output


def check_output(out, expout):
    lines = [line for line in out.decode().split('\n') if line]
    if expout:
        with open(join(SOURCESDIR, expout), 'r') as f:
            expected = [line for line
                        in map(lambda s: s.strip(), f.readlines()) if line]
            if expected != lines:
                if debug:
                    print(' -- expected --')
                    print(expected)
                    print(' -- got --')
                    print(lines)
                error('The output is not as expected')

        return

    # the default expected output
    passed = False
    failed = False

    for l in lines:
        if l == 'Assertion PASSED':
            passed = True
        elif l == 'Assertion FAILED':
            failed = True

    if not passed and not failed:
        error('Assertion was not called')
    if not passed or failed:
        error('Assertion failed!')


def execute(bccode, expout=None):
    lli = join(LLVM_TOOLS_DIR, "lli")
    out, err, exitcode = command_output([lli, bccode])
    if debug:
        print("--- stdout ---")
        print(out.decode())
        print("--- stderr ---")
        print(err.decode())
        print("--- exitcode {0} ---".format(exitcode))
    if exitcode != 0:
        error("Executing the bitcode failed!")

    check_output(out, expout)


def get_variations(rem=list(configs), result=[]):
    """ Get all possible variations of the parameters """
    if not rem:
        return result

    if result == []:
        result = [["{0}={1}".format(rem[0], c)] for c in configs[rem[0]]]
    else:
        tmp = result
        result = []
        for c in configs[rem[0]]:
            result += [x + ["{0}={1}".format(rem[0], c)] for x in tmp]

    return get_variations(rem[1:], result)


def _test_enabled(test, setup):
    for p in test.requiredparams:
        if p not in setup:
            return False

    return True


def run_test(test, bccode, optafter, linkafter, args):
    toremove = []

    bccode = slice(bccode, args + test.addparams)
    toremove.append(bccode)

    if optafter:
        assert False
    if linkafter:
        bccode = link(bccode, linkafter)
        toremove.append(bccode)

    if test.optafter:
        bccode = opt(bccode, t.optafter)

    execute(bccode, test.expectedoutput)

    for f in toremove:
        unlink(f)


if __name__ == "__main__":
    from tests import tests

    if len(argv) <= 1:
        error('Usage: {0} test-name [args to slicer]'.format(argv[0]))

    if have_svf:
        configs['-pta'].append('svf')

    try:
        t = tests[argv[1]]
    except KeyError:
        error("Unknown test name: '{0}'".format(argv[1]))

    if 'debug' in argv[0]:
        debug = True

    set_environment()

    # create tmpdir for given test
    from os import mkdir
    rmtree(argv[1], ignore_errors=True)  # just to be sure it does not exist
    mkdir(argv[1])
    chdir(argv[1])

    # compile the source
    bccode = compile(join(SOURCESDIR, t.source),
                     params=t.compilerparams)

    optbefore, linkbefore, optafter, linkafter = [], [], [], []

    if t.linkbefore:
        for l in t.linkbefore:
            bctolink = compile(join(SOURCESDIR, l),
                               params=t.compilerparams)
            linkbefore.append(bctolink)
    if t.linkafter:
        for l in t.linkafter:
            bctolink = compile(join(SOURCESDIR, l),
                               params=t.compilerparams)
            linkafter.append(bctolink)

    if t.optbefore:
        bccode = opt(bccode, t.optbefore)

    if linkbefore:
        bccode = link(bccode, linkbefore)

    # always link test_assert() after slicing
    assertbc = compile(join(SOURCESDIR, '..', 'test_assert.c'),
                       params=t.compilerparams)
    linkafter.append(assertbc)

    # RUN!
    args = argv[2:]
    if args:
        stdout.write("Executing setup: {0} ... {1}".format(" ".join(args),
                                                           "\n" if debug else ""))
        run_test(t, bccode[:], optafter, linkafter, args)
        print('OK!')
    else:
        for setup in get_variations():
            if not _test_enabled(t, setup):
                print("Skipping setup", " ".join(setup))
                continue

            stdout.write("Executing setup: {0} ... {1}".format(" ".join(setup),
                                                               "\n" if debug else ""))
            run_test(t, bccode[:], optafter, linkafter, setup)
            print('OK!')

    # cleanup
    cleanup()
