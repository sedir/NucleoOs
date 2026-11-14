// ui-host: force-included first (g++ -include). Bridges MinGW to the POSIX bits the firmware uses and
// makes wall-clock time SCRIPTED (host_time) so screenshots are deterministic.
#pragma once
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS 1      // localtime_r / gmtime_r in MinGW's <time.h>
#endif
#include <time.h>
#include <sys/stat.h>
#include <direct.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
time_t host_time(time_t *out);              // host_stubs.cpp: the scene's wall clock (UTC)
#ifdef __cplusplus
}
#endif
#define time(p) host_time(p)
#ifdef _WIN32
#define mkdir(p, m) _mkdir(p)
#else
#define _mkgmtime timegm
#define _rmdir rmdir
#define _tzset tzset
#define _putenv(s) putenv((char *)(s))
#include <unistd.h>
#include <stdlib.h>
#endif
