#include "dg/tools/llvm-slicer-utils.h"

#include "dg/tools/llvm-slicer-opts.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <string>

std::vector<std::string> splitList(const std::string &opt, char sep) {
    std::vector<std::string> ret;
    if (opt.empty())
        return ret;

    size_t old_pos = 0;
    size_t pos = 0;
    while (true) {
        old_pos = pos;

        pos = opt.find(sep, pos);
        ret.push_back(opt.substr(old_pos, pos - old_pos));

        if (pos == std::string::npos)
            break;
        ++pos;
    }

    return ret;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
splitStringVector(std::vector<std::string> &vec,
                  std::function<bool(std::string &)> cmpFunc) {
    std::vector<std::string> part1;
    std::vector<std::string> part2;

    for (auto &str : vec) {
        if (cmpFunc(str)) {
            part1.push_back(std::move(str));
        } else {
            part2.push_back(std::move(str));
        }
    }

    return {part1, part2};
}

void replace_suffix(std::string &fl, const std::string &with) {
    if (fl.size() > 2) {
        if (fl.compare(fl.size() - 2, 2, ".o") == 0)
            fl.replace(fl.end() - 2, fl.end(), with);
        else if (fl.compare(fl.size() - 3, 3, ".bc") == 0)
            fl.replace(fl.end() - 3, fl.end(), with);
        else
            fl += with;
    } else {
        fl += with;
    }
}

std::unique_ptr<llvm::Module> parseModule(const char *tool,
                                          llvm::LLVMContext &context,
                                          const SlicerOptions &options) {
    llvm::SMDiagnostic smd;

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 5
    auto _m = llvm::parseIRFile(options.inputFile, smd, context);
    auto m = std::unique_ptr<llvm::Module>(_m);
#else
    auto m = llvm::parseIRFile(options.inputFile, smd, context);
#endif

    if (!m) {
        llvm::errs() << "Failed parsing '" << options.inputFile << "' file:\n";
        smd.print(tool, llvm::errs());
    }

    return m;
}

#ifndef USING_SANITIZERS
void setupStackTraceOnError(int argc, char *argv[]) {
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 9
    llvm::sys::PrintStackTraceOnErrorSignal();
#else
    llvm::sys::PrintStackTraceOnErrorSignal(llvm::StringRef());
#endif
    llvm::PrettyStackTraceProgram X(argc, argv);
}
#else
void setupStackTraceOnError(int, char **) {}
#endif // not USING_SANITIZERS
