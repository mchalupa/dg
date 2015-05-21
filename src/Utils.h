#ifndef _DG_UTILS_H_
#define _DG_UTILS_H_

#if (DEBUG_ENABLED)
#include <cstdio>
#if(!DBG)
#define DBG(...) do { fprintf(stderr, "DBG: ");\
                      fprintf(stderr, __VA_ARGS__);\
                      fprintf(stderr, "\n"); \
                 } while(0)
#else
#define DBG(...)
#endif // if(!(DBG)
#endif // DEBUG_ENABLED

#endif // _DG_UTILS_H_
