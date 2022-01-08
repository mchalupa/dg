#ifndef DG_TIME_UTILS_H_
#define DG_TIME_UTILS_H_

#include <chrono>
#include <iostream>

namespace dg {
namespace debug {

class TimeMeasure {
    using Clock = std::chrono::steady_clock;
    using TimePointT = Clock::time_point;
    using DurationT = Clock::duration;

    TimePointT s, e;
    DurationT elapsed;

    constexpr static auto ms_in_sec =
            std::chrono::milliseconds(std::chrono::seconds{1}).count();

  public:
    TimeMeasure() = default;

    void start() { s = Clock::now(); };

    void stop() { e = Clock::now(); };

    const DurationT &duration() {
        elapsed = e - s;
        return elapsed;
    }

    void report(const std::string &prefix = "", std::ostream &out = std::cerr) {
        // compute the duration
        duration();

        out << prefix << " ";

        const auto msec =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                        .count() %
                ms_in_sec;
        const auto sec =
                std::chrono::duration_cast<std::chrono::seconds>(elapsed)
                        .count();
        out << sec << " sec " << msec << " ms" << std::endl;
    }
};
} // namespace debug
} // namespace dg

#endif // DG_TIME_UTILS_H_
