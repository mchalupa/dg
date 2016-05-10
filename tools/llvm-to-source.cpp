#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/Config/llvm-config.h>
#if (LLVM_VERSION_MINOR < 5)
 #include <llvm/DebugInfo.h>
#else
 #include <llvm/DebugInfo/DIContext.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

using namespace llvm;

static void get_lines_from_module(const Module *M, std::set<unsigned>& lines)
{
    // iterate over all instructions
    for (const Function& F : *M) {
        for (const BasicBlock& B : F) {
            for (const Instruction& I : B) {
                const DebugLoc& Loc = I.getDebugLoc();
                lines.insert(Loc.getLine());
            }
        }
    }

    // iterate over all globals
    /*
    for (const GlobalVariable& G : M->globals()) {
        const DebugLoc& Loc = G.getDebugLoc();
        lines.insert(Loc.getLine());
    }
    */
}

static bool should_print(const char *buf, unsigned linenum,
                         std::set<unsigned>& lines)
{
    static unsigned bnum = 0;
    std::istringstream ss(buf);
    char c;

    ss >> std::skipws >> c;
    if (c == '{')
        ++bnum;
    else if (c == '}')
        --bnum;

    // bnum == 1 means we're in function
    if (bnum == 1) {
        // opening bracket
        if (c == '{')
            return true;

        // empty line
        if (*buf == '\n')
            return true;
    }

    // closing bracket
    if (bnum == 0 && c == '}')
        return true;

    if (lines.count(linenum) != 0)
        return true;

    return false;
}

static void print_lines(std::ifstream& ifs, std::set<unsigned>& lines)
{
    char buf[1024];
    unsigned cur_line = 1;
    while (!ifs.eof()) {
        ifs.getline(buf, sizeof buf);

        if (should_print(buf, cur_line, lines)) {
            std::cout << cur_line << ": ";
            std::cout << buf << "\n";
        }

        if (ifs.bad()) {
            errs() << "An error occured\n";
            break;
        }

        ++cur_line;
    }
}

static void print_lines_numbers(std::set<unsigned>& lines)
{
    for (unsigned ln : lines)
        std::cout << ln << "\n";
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    LLVMContext context;
    SMDiagnostic SMD;

    const char *source = NULL;
    const char *module = NULL;

    if (argc < 2 || argc > 3 ) {
        errs() << "Usage: module [source_code]\n";
        return 1;
    }

    module = argv[1];
    if (argc == 3)
        source = argv[2];

#if (LLVM_VERSION_MINOR < 5)
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = &*_M;
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    // FIXME find out if we have debugging info at all
    // no difficult machineris - just find out
    // which lines are in our module and print them
    std::set<unsigned> lines;
    get_lines_from_module(M, lines);

    if (!source)
        print_lines_numbers(lines);
    else {
        std::ifstream ifs(source);
        if (!ifs.is_open() || ifs.bad()) {
            errs() << "Failed opening given source file: " << source << "\n";
            return 1;
        }

        print_lines(ifs, lines);
        ifs.close();
    }

    return 0;
}
