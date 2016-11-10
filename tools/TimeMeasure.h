#ifndef _DG_UTILS_H_
#define _DG_UTILS_H_

#include <cstdio>
#include <ctime>

namespace dg {
namespace debug {

class TimeMeasure
{
    struct timespec s, e, r;
    clockid_t clockid;

public:
    TimeMeasure(clockid_t clkid = CLOCK_MONOTONIC)
        : clockid(clkid) {}

    void start() {
        clock_gettime(clockid, &s);
    };

    void stop() {
        clock_gettime(clockid, &e);
    };

    const struct timespec& duration()
    {
        r.tv_sec = e.tv_sec - s.tv_sec;
        if (e.tv_nsec > s.tv_nsec)
            r.tv_nsec = e.tv_nsec - s.tv_nsec;
        else {
            --r.tv_sec;
            r.tv_nsec = 1000000000 - e.tv_nsec;
        }

        return r;
    }

    void report(const char *prefix = nullptr, FILE *out = stderr)
    {
        // compute the duration
        duration();

        if (prefix)
            fprintf(out, "%s ", prefix);

        fprintf(out, "%lu sec %lu ms\n", r.tv_sec, r.tv_nsec / 1000000);
        fflush(out);
    }
};
} // namespace debug
} // namespace dg

#endif // _DG_UTILS_H_
