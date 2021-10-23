#!/usr/bin/env python3

from os import chdir, environ, getcwd
from os.path import abspath, basename, dirname, join
from shutil import rmtree
from subprocess import DEVNULL, PIPE, Popen
from sys import argv, stdout

clang_has_sanitizers = False
debug = False
have_svf = False

# going to be (possibly) re-set in set_environment()
DG_TOOLS_DIR = "../../tools/"
TEST_SOURCES_DIR = "../sources/"
LLVM_TOOLS_DIR = ""


def parse_cmake_cache(cmakecache):
    with open(cmakecache, 'r') as f:
        for line in f:
            if line.startswith('SVF_DIR'):
                have_svf = True
            elif line.startswith('CLANG_HAS_SANITIZERS'):
                global clang_has_sanitizers
                clang_has_sanitizers = True
            elif line.startswith('dg_SOURCE_DIR'):
                parts = line.split('=')
                global TEST_SOURCES_DIR
                TEST_SOURCES_DIR = abspath(join(parts[1].strip(),
                                                'tests/slicing/sources/'))
            elif line.startswith('dg_BINARY_DIR'):
                parts = line.split('=')
                global DG_TOOLS_DIR
                DG_TOOLS_DIR = abspath(join(parts[1].strip(), 'tools/'))
            elif line.startswith('LLVM_TOOLS_DIR'):
                parts = line.split('=')
                global LLVM_TOOLS_DIR
                LLVM_TOOLS_DIR = parts[1].strip()


configs = {
#   '-dda': ['rd', 'ssa'],
    '-pta': ['fi', 'fs', 'inv'],
    '-cd-alg': ['ntscd', 'classic'],
}


def command(cmd, env=environ):
    if debug:
        print("> " + "  ".join(cmd), flush=True)
        p = Popen(cmd, env=env)
    else:
        p = Popen(cmd, stdout=DEVNULL, stderr=DEVNULL, env=env)
    return p.wait()


def command_output(cmd, env=environ):
    if debug:
        print("> " + "  ".join(cmd), flush=True)
    p = Popen(cmd, stdout=PIPE, stderr=PIPE, env=env)
    out, err = p.communicate()
    return out, err, p.poll()


def cleanup():
    oldpath = getcwd()
    chdir('..')
    rmtree(oldpath)


def error(msg):
    from sys import exit, stderr
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

    clang = join(LLVM_TOOLS_DIR, 'clang')
    test_assert_h = join(TEST_SOURCES_DIR, '..', "test_assert.h")
    ret = command([clang, "-include", test_assert_h, "-emit-llvm", "-std=c11",
                   "-fno-strict-aliasing", "-c", source, "-o", output]
                  + params)
    if ret != 0:
        error('Failed executing ' + clang)

    return output


def slice(bccode, args):
    output = bccode + ".sliced"
    slicer = join(DG_TOOLS_DIR, "llvm-slicer")

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
        error('Failed executing ' + llvm_link)

    return output


def opt(bccode, passes, output=None):
    if output is None:
        output = bccode + ".opt"

    opt = join(LLVM_TOOLS_DIR, "opt")
    cmd = [opt, bccode, "-o", output] + passes

    if command(cmd) != 0:
        error('Failed executing ' + opt)

    return output


def check_output(out, err, exitcode, expout):
    if debug:
        print("--- stdout ---")
        print(out.decode())
        print("--- stderr ---")
        print(err.decode())
        print("--- exitcode {0} ---".format(exitcode))

    if exitcode != 0:
        error("Executing the code failed!")

    lines = [line for line in out.decode().split('\n') if line]
    if expout:
        with open(join(TEST_SOURCES_DIR, expout), 'r') as f:
            expected = [line for line
                        in map(lambda s: s.strip(), f.readlines()) if line]
            if expected != lines:
                if debug:
                    print(' -- expected --')
                    print(expected)
                    print(' -- got --')
                    print(lines)
                error('The output is not as expected')

        print('OK!\n')
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

    print('OK!\n')


def execute(bccode, expout=None):
    lli = join(LLVM_TOOLS_DIR, 'lli')
    ret = command_output([lli, bccode])
    check_output(*ret, expout)


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
    bccode = slice(bccode, args + test.addparams)

    if optafter:
        assert False

    if linkafter:
        bccode = link(bccode, linkafter)

    if test.optafter:
        bccode = opt(bccode, t.optafter)

    execute(bccode, test.expectedoutput)


def sanity_check(test):
    """Check that the test runs and has the expected output without slicing."""

    clang = join(LLVM_TOOLS_DIR, 'clang')
    cmd = [clang, join(TEST_SOURCES_DIR, t.source),
           join(TEST_SOURCES_DIR, '..', 'test_assert.c'),
           '-include', join(TEST_SOURCES_DIR, '..', 'test_assert.h'),
           '-std=c11', '-fno-strict-aliasing', '-g', '-Werror',
           '-o', 'sanity'] + test.compilerparams

    if clang_has_sanitizers:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-fno-sanitize-recover=all']

    if test.linkbefore:
        cmd += [join(TEST_SOURCES_DIR, x) for x in test.linkbefore]
    if test.linkafter:
        cmd += [join(TEST_SOURCES_DIR, x) for x in test.linkafter]

    if command(cmd) != 0:
        error('Failed executing ' + clang)

    env = {'ASAN_OPTIONS': 'detect_leaks=0',
           'UBSAN_OPTIONS': 'print_stacktrace=1'}
    ret = command_output(['./sanity'], env=env)
    check_output(*ret, test.expectedoutput)


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

    # check that the unsliced original works as intended
    sanity_check(t)

    # compile the source
    bccode = compile(join(TEST_SOURCES_DIR, t.source),
                     params=t.compilerparams)

    optbefore, linkbefore, optafter, linkafter = [], [], [], []

    if t.linkbefore:
        for l in t.linkbefore:
            bctolink = compile(join(TEST_SOURCES_DIR, l),
                               params=t.compilerparams)
            linkbefore.append(bctolink)

    if t.linkafter:
        for l in t.linkafter:
            bctolink = compile(join(TEST_SOURCES_DIR, l),
                               params=t.compilerparams)
            linkafter.append(bctolink)

    if t.optbefore:
        bccode = opt(bccode, t.optbefore)

    if linkbefore:
        bccode = link(bccode, linkbefore)

    # always link test_assert() after slicing
    assertbc = compile(join(TEST_SOURCES_DIR, '..', 'test_assert.c'),
                       params=t.compilerparams)
    linkafter.append(assertbc)

    # RUN!
    args = argv[2:]
    endline = "\n" if debug else ""
    if args:
        stdout.write("Executing setup: {0} ... {1}".format(" ".join(args),
                                                           endline))
        run_test(t, bccode, optafter, linkafter, args)
    else:
        for setup in get_variations():
            if not _test_enabled(t, setup):
                print("Skipping setup", " ".join(setup))
                continue

            stdout.write("Executing setup: {0} ... {1}".format(" ".join(setup),
                                                               endline))
            run_test(t, bccode, optafter, linkafter, setup)

    # cleanup
    cleanup()
