#ifndef DG_UTIL_DEBUG_H_
#define DG_UTIL_DEBUG_H_

#ifdef DEBUG_ENABLED
#include <cassert>
#include <ctime>
#include <iostream>
#endif

namespace dg {
namespace debug {

#ifdef DEBUG_ENABLED

extern unsigned _debug_lvl;
extern unsigned _ind;

namespace {
inline unsigned &_getDebugLvl() { return _debug_lvl; }

inline void _setDebugLvl(unsigned int x) { _getDebugLvl() = x; }

inline unsigned &_getInd() { return _ind; }

inline std::ostream &_stream() { return std::cerr; }

inline void _dump_ind() {
    auto ind = _getInd();
    for (unsigned i = 0; i < ind; ++i)
        _stream() << " ";
}

inline void _dump_prefix(const char *domain, const char *add = nullptr) {
    std::cerr << "[" << std::clock() << "]";
    if (domain)
        _stream() << "[" << domain << "]";

    _stream() << " ";

    _dump_ind();
    if (add) {
        _stream() << add;
    }
}
} // namespace

/*
static inline bool dbg_should_print(unsigned int x) {
    return _getDebugLvl() > x;
}
*/

inline void dbg_enable() { _setDebugLvl(1); }

inline bool dbg_is_enabled() { return _getDebugLvl() > 0; }

inline std::ostream &dbg_section_begin(const char *domain = nullptr) {
    _dump_prefix(domain, "-> ");
    _getInd() += 3;
    return _stream();
}

inline std::ostream &dbg_section_end(const char *domain = nullptr) {
    assert(_getInd() >= 3);
    _getInd() -= 3;
    _dump_prefix(domain, "<- ");
    return _stream();
}

inline std::ostream &dbg(const char *domain = nullptr) {
    _dump_prefix(domain);
    return _stream();
}

#define DBG_ENABLE()                                                           \
    do {                                                                       \
        ::dg::debug::dbg_enable();                                             \
    } while (0)

#define DBG_SECTION_BEGIN(dom, S)                                              \
    do {                                                                       \
        if (::dg::debug::dbg_is_enabled()) {                                   \
            ::dg::debug::dbg_section_begin((#dom)) << S << "\n";               \
        }                                                                      \
    } while (0)

#define DBG_SECTION_END(dom, S)                                                \
    do {                                                                       \
        if (::dg::debug::dbg_is_enabled()) {                                   \
            ::dg::debug::dbg_section_end((#dom)) << S << "\n";                 \
        }                                                                      \
    } while (0)

#define DBG(dom, S)                                                            \
    do {                                                                       \
        if (::dg::debug::dbg_is_enabled()) {                                   \
            ::dg::debug::dbg((#dom)) << S << "\n";                             \
        }                                                                      \
    } while (0)

#else // not DEBUG_ENABLED

#define DBG_ENABLE()
#define DBG_SECTION_BEGIN(dom, S)
#define DBG_SECTION_END(dom, S)
#define DBG(dom, S)

#endif // DEBUG_ENABLED

} // namespace debug
} // namespace dg

#endif // DG_UTIL_DEBUG_H_
