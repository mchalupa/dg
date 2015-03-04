/// XXX add licence
//

#include "DGUtil.h"

#ifdef DEBUG

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dg {
namespace debug {

enum dbg_domain dbg_enabled;

void init(void)
{
	// already initialized
	if (dbg_enabled)
		return;

	const char *env = getenv("DG_DEBUG");
	if (env) {
		if (strcmp(env, "all") == 0 || strcmp(env, "1") == 0)
			dbg_enabled = ALL;
		else if (strcmp(env, "control") == 0 || strcmp(env, "c") == 0)
			dbg_enabled = CONTROL;
		else if (strcmp(env, "dependence") == 0 || strcmp(env, "d") == 0)
			dbg_enabled = DEPENDENCE;
	}
}

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
