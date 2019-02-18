#ifndef ARGS_H
#define ARGS_H

#include <vector>
#include <string>
#include <stdexcept>
#include <sstream>

class Arguments;

class Option {
    const char shortOption;
    const std::string longOption;
    const std::string description_;
    const bool hasArgument;
    bool present = false;
    std::string argument;

    friend Arguments;

public:
    Option(char shortOption, const std::string &longOption, const std::string description, bool hasArgument)
        :shortOption(shortOption),
         longOption(longOption),
         description_(description),
         hasArgument(hasArgument)
    {}

    std::string description() const {
        return description_;
    }

    int getInt() const {
        return std::stoi(argument, nullptr, 0); // TODO catch and rethrow with a reasonable error message
    }

    float getFloat() const {
        return std::stof(argument); // TODO catch and rethrow with a reasonable error message
    }

    double getDouble() const {
        return std::stod(argument); // TODO catch and rethrow with a reasonable error message
    }

    std::string getString() const {
        return argument;
    }

    explicit operator bool() const {
        return present;
    }
};

class Arguments {
    std::vector<Option> options;

public:

    void add(char shortOption, const std::string &longOption, const std::string &description, bool hasArgument = false) {
        options.emplace_back(shortOption, longOption, description, hasArgument);
    }

    void parse(const int argc, const char *argv[]) {
        using namespace std::string_literals;
        auto i = 1;
        while (i < argc) {
            if (isShortOption(argv[i])) {
                parseShortOptions(argc, argv, i);
            } else if (isLongOption(argv[i])) {
                parseLongOption(argc, argv, i);
            } else {
                throw std::invalid_argument("Option \""s + argv[i] + "\" has invalid format. Valid formats are: -o [argumnent]; --option [argument]; --option[=argument]"s );
            }
        }
    }

    Option& operator()(const std::string &opt){
        using namespace std::string_literals;
        for (auto &option : options) {
            if (option.longOption == opt) {
                return option;
            }
        }
        throw std::logic_error("Option "s + opt + " does not exist"s);
    }

private:

    bool isShortOption(std::string str) {
        return str.length() > 1
            && str[0] == '-'
            && str[1] != '-';
    }

    bool isLongOption(std::string str) {
        return str[0] == '-'
            && str[1] == '-';
    }

    Option &findOption(char shortOption) {
        using namespace std::string_literals;
        for (auto &option : options) {
            if (option.shortOption == shortOption) {
                return option;
            }
        }
        throw std::invalid_argument("Option "s + "-"s + std::string(1,shortOption) + " does not exist"s);
    }

    Option &findOption(const std::string &longOption) {
        using namespace std::string_literals;
        for (auto &option : options) {
            if (option.longOption == longOption) {
                return option;
            }
        }
        throw std::invalid_argument("Option "s + "--"s + longOption + " does not exist"s);
    }

    void parseShortOptions(const int argc, const char *argv[], int &index) {
        using namespace std;
        using namespace std::string_literals;
        string shortOpts = argv[index];
        shortOpts.erase(shortOpts.begin());
        for (unsigned i = 0; i < shortOpts.length(); ++i) {
            Option &option = findOption(shortOpts[i]);
            option.present = true;
            if (option.hasArgument && i == shortOpts.length() - 1) {
                if (index + 1 >= argc) {
                    throw logic_error("Option -"s + string(1,option.shortOption) + " needs an argument");
                } else {
                    option.argument = argv[index+1];
                    index++;
                }
            }
        }
        index++;
    }

    void parseLongOption(const int argc, const char *argv[], int &index) {
        using namespace std;
        using namespace std::string_literals;
        string longOpt = argv[index];
        longOpt.erase(longOpt.begin(), longOpt.begin() + 2);
        stringstream stream(longOpt);
        string name;
        getline(stream, name, '=');
        Option &option = findOption(name);
        option.present = true ;
        if (option.hasArgument) {
            string arg;
            getline(stream, arg);
            if (arg.empty()) {
                if (index + 1 >= argc) {
                    throw logic_error("Option --"s + name + " needs an argument");
                } else {
                    option.argument = argv[index+1];
                    index++;
                }
            } else {
                option.argument = arg;
            }
        }
        index++;
    }
};

#endif // ARGS_H
