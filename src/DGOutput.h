/// XXX add licence
//

#ifndef _DG_OUTPUT_H_
#define _DG_OUTPUT_H_

#ifdef DEBUG

namespace dg {
namespace debug {


enum dbg_domain {
	ALL = 1,
};

enum dgb_domain dbg_enabled;

void _dbg(enum dbg_domain domain, const char *prefix, const char *fmt, ...);

#define dbg(...) _dbg(ALL, "ALL", __VA_ARGS__)


} // namespace debug
} // namespace dg

#else // DEBUG

#define dbg(...)

#endif // DEBUG

#endif // _DG_OUTPUT_H_
