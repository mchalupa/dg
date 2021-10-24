#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stack>

#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/DebugInfo.h>
#else
#include <llvm/DebugInfo/DIContext.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>

llvm::cl::opt<std::string> inputFile(llvm::cl::Positional, llvm::cl::Required,
                                     llvm::cl::desc("<input file>"),
                                     llvm::cl::init(""));

llvm::cl::opt<std::string> sourceFile(llvm::cl::Positional, llvm::cl::Optional,
                                      llvm::cl::desc("[source code]"),
                                      llvm::cl::init(""));

// lines with matching braces
std::vector<std::pair<unsigned, unsigned>> matching_braces;
// mapping line->index in matching_braces
std::map<unsigned, unsigned> nesting_structure;

static void get_lines_from_module(const llvm::Module &M,
                                  std::set<unsigned> &lines) {
    // iterate over all instructions
    for (const auto &F : M) {
        for (const auto &B : F) {
            for (const auto &I : B) {
                const auto &Loc = I.getDebugLoc();
                // Make sure that the llvm istruction has corresponding dbg LOC
#if ((LLVM_VERSION_MAJOR > 3) ||                                               \
     ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR > 6)))
                if (Loc)
#else
                if (Loc.getLine() > 0)
#endif
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

static void get_nesting_structure(const std::string &source) {
    std::ifstream ifs(source);
    if (!ifs.is_open() || ifs.bad()) {
        llvm::errs() << "Failed opening given source file: " << source << "\n";
        abort();
    }

    char ch;
    unsigned cur_line = 1;
    unsigned idx;
    std::stack<unsigned> nesting;
    while (ifs.get(ch)) {
        switch (ch) {
        case '\n':
            ++cur_line;
            if (!nesting.empty())
                nesting_structure.emplace(cur_line, nesting.top());
            break;
        case '{':
            nesting.push(matching_braces.size());
            matching_braces.emplace_back(cur_line, 0);
            break;
        case '}':
            idx = nesting.top();
            assert(idx < matching_braces.size());
            assert(matching_braces[idx].second == 0);
            matching_braces[idx].second = cur_line;
            nesting.pop();
            break;
        default:
            break;
        }
    }

    ifs.close();
}

static void print_lines(std::ifstream &ifs, std::set<unsigned> &lines) {
    char buf[1024];
    unsigned cur_line = 1;
    while (!ifs.eof()) {
        ifs.getline(buf, sizeof buf);

        if (lines.count(cur_line) > 0) {
            std::cout << cur_line << ": ";
            std::cout << buf << "\n";
        }

        if (ifs.bad()) {
            llvm::errs() << "An error occured\n";
            break;
        }

        ++cur_line;
    }
}

static void print_lines_numbers(std::set<unsigned> &lines) {
    for (unsigned ln : lines)
        std::cout << ln << "\n";
}

int main(int argc, char *argv[]) {
    llvm::cl::ParseCommandLineOptions(argc, argv);

    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    std::unique_ptr<llvm::Module> M =
            llvm::parseIRFile(inputFile, SMD, context);
    if (!M) {
        llvm::errs() << "Failed parsing '" << inputFile << "' file:\n";
        SMD.print(argv[0], llvm::errs());
        return 1;
    }

    // FIXME find out if we have debugging info at all
    // no difficult machineris - just find out
    // which lines are in our module and print them
    std::set<unsigned> lines;
    get_lines_from_module(*M, lines);

    if (sourceFile.empty())
        print_lines_numbers(lines);
    else {
        get_nesting_structure(sourceFile);
        /* fill in the lines with braces */
        /* really not efficient, but easy */
        size_t old_size;
        do {
            old_size = lines.size();
            std::set<unsigned> new_lines;

            for (unsigned i : lines) {
                new_lines.insert(i);
                auto it = nesting_structure.find(i);
                if (it != nesting_structure.end()) {
                    auto &pr = matching_braces[it->second];
                    new_lines.insert(pr.first);
                    new_lines.insert(pr.second);
                }
            }

            lines.swap(new_lines);
        } while (lines.size() > old_size);

        std::ifstream ifs(sourceFile);
        if (!ifs.is_open() || ifs.bad()) {
            llvm::errs() << "Failed opening given source file: " << sourceFile
                         << "\n";
            return 1;
        }

        // print_lines_numbers(lines);
        print_lines(ifs, lines);
        ifs.close();
    }

    return 0;
}
