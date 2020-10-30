#include <vector>
#include <string>

#include "dg/tools/llvm-slicer-utils.h"

std::vector<std::string> splitList(const std::string& opt, char sep) {
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
        else
            ++pos;
    }

    return ret;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
splitStringVector(std::vector<std::string>& vec,
                  std::function<bool(std::string&)> cmpFunc)
{
    std::vector<std::string> part1;
    std::vector<std::string> part2;

    for (auto& str : vec) {
        if (cmpFunc(str)) {
            part1.push_back(std::move(str));
        } else {
            part2.push_back(std::move(str));
        }
    }

    return {part1, part2};
}

void replace_suffix(std::string& fl, const std::string& with) {
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

