#ifndef _TEST_RUNNER_H_
#define _TEST_RUNNER_H_

#include <vector>
#include <string>
#include <iostream>

namespace dg {
namespace tests {

class Test
{
    std::string name;

    bool failed;

public:
    Test(const std::string& name)
        :name(name), failed(false)
    {}

    virtual bool test() = 0;

    void check(bool cond, const char *fmt = nullptr, ...)
    {
        if (!cond) {
            fail(fmt);
        }
    }

    void fail(const char *fmt, ...)
    {
        if (fmt) {
        }

        failed = true;
    }

    // return true if everything is ok
    bool run()
    {
        bool result;
        std::cout << "Running: " << name << " -> ";
        // func returning false means failure
        result = test();

        return result && !failed;
    }

    bool operator()() { return run(); }
};

class TestRunner
{
    std::vector<Test *> tests;
    unsigned int failed;

public:
    TestRunner() : failed(0) {}
    ~TestRunner()
    {
        for (Test *t : tests)
            delete t;
    }

    void add(Test *t) { tests.push_back(t); }
    void report(bool succ)
    {
        if (succ) {
            std::cout << "OK" << std::endl;
        } else {
            std::cout << "FAILED" << std::endl;
            ++failed;
        }
    }

    bool run()
    {
        for (Test *t : tests) {
            bool result = t->run();
            report(result);
        }

        return failed != 0;
    }

    bool operator()() { return run(); }
};

} // namespace tests
} // namespace dg

#endif // _TEST_RUNNER_H_
