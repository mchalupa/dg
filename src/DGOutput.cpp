/// XXX add licence
//

#include "DGOutput.h"

#ifdef DEBUG

namespace dg {
namespace debug {

void _dbg(enum dbg_domain domain, const char *prefix, const char *fmt, ...)
{
	va_list args;

	if (dbg_enabled != ALL && dbg_enabled != domain)
		return;

	fprintf(stderr, "%s: ", prefix);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

#endif // DEBUG

} // namespace debug
} // namespace dg
