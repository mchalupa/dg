/// XXX add licence
//

#ifndef _DG_OUTPUT_H_
#define _DG_OUTPUT_H_

#ifdef DEBUG_ENABLED

namespace dg {
namespace debug {

enum dbg_domain {
    ALL = 1,
    CONTROL,
    DEPENDENCE,
    NODES,
};

void init(void);
void _dbg(enum dbg_domain domain, const char *prefix, const char *fmt, ...);

#define DBG(domain, ...) dg::debug::_dbg((dg::debug::domain), #domain, __VA_ARGS__)


} // namespace debug
} // namespace dg

#else // DEBUG

#define DBG(domain, ...)

#endif // DEBUG

#ifndef NULL
#define NULL 0
#endif

#endif // _DG_OUTPUT_H_
