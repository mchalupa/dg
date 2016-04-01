#ifndef _TEST_RUNNER_H_
#define _TEST_RUNNER_H_

#include <vector>
#include <string>
#include <cstdio>
#include <unistd.h>
#include <stdarg.h>
#include <cstring>

namespace dg {
namespace tests {

class Test
{
    std::string name;

    unsigned int failed;

    void _fail(const char *fmt, va_list vl)
    {
        putc('\t', stderr);
        vfprintf(stderr, fmt, vl);
        putchar('\n');
        fflush(stderr);

        ++failed;
    }

public:
    Test(const std::string& name)
        :name(name), failed(0)
    {}

    virtual ~Test() {}

    virtual void test() = 0;

#define check(cond, ...)                                            \
        if (!(cond)) {                                              \
            fprintf(stderr, "Failed %s:%u: ",                       \
                    basename(__FILE__), __LINE__);                  \
            fail("" __VA_ARGS__);                                   \
        }

    void fail(const char *fmt, ...)
    {
        va_list vl;

        if (fmt) {
            va_start(vl, fmt);
            _fail(fmt, vl);
            va_end(vl);

            fflush(stderr);
        }
    }

    // return true if everything is ok
    bool run()
    {
        printf("-- Running: %s\n", name.c_str());
        test();

        if (failed != 0) {
            printf("\tTotal %u failures in this test\n", failed);
        }

        return failed == 0;
    }

    bool operator()() { return run(); }
    const std::string& getName() const { return name; }
};

class TestRunner
{
    std::vector<Test *> tests;
    unsigned int failed;

    bool istty;

public:
    TestRunner() : failed(0)
    {
        istty = isatty(fileno(stdout));
    }

    ~TestRunner()
    {
        for (Test *t : tests)
            delete t;
    }

    void add(Test *t) { tests.push_back(t); }
    bool run()
    {
        for (Test *t : tests) {
            bool result = t->run();
            report(result);
        }

        if (failed != 0)
            printf("\n%u test(s) failed\n", failed);
        else
            printf("\nAll test passed! o/\\o\n");

        fflush(stdout);

        return failed != 0;
    }

    bool operator()() { return run(); }
private:
    #define RED     "\033[31m"
    #define GREEN   "\033[32m"
    void set_color(const char *color = nullptr)
    {
        if (!istty)
            return;

        if (color)
            printf("%s", color);
        else
            printf("\033[0m");
    }

    void report(bool succ)
    {
        printf("-- ---> ");
        if (succ) {
            set_color(GREEN);
            printf("OK\n");
        } else {
            set_color(RED);
            printf("FAILED\n");
            ++failed;
        }

        set_color(); // reset color
        fflush(stdout);
    }
};

} // namespace tests
} // namespace dg

#endif // _TEST_RUNNER_H_
