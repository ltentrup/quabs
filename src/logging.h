#ifndef __caqe__logging__
#define __caqe__logging__

#include <stdio.h>
#include <stdbool.h>

#include "config.h"

typedef enum {
    VERBOSITY_NONE = 0,  // errors only
    VERBOSITY_NORMAL,    // warnings/prints (standard)
    VERBOSITY_HIGH,      // info
    VERBOSITY_ALL        // debug
} verbosity;

void logging_fatal(const char* format, ...)  __attribute__ ((noreturn));
void logging_error(const char* format, ...);
void logging_warn(const char* format, ...);
void logging_print(const char* format, ...);

#ifdef LOGGING
void logging_info(const char* format, ...);
void logging_debug(const char* format, ...);
#else
#define logging_info(...)
#define logging_debug(...)
#endif

#ifndef NO_API_ASSERTIONS
void api_expect(bool assertion, const char* format, ...);
#else
#define api_expect(assertion, ...)
#endif

#ifndef NDEBUG
void fixme(const char* format, ...);
#else
#define fixme(format, ...)
#endif

void logging_set_verbosity(verbosity);

#ifdef LOGGING
verbosity logging_get_verbosity(void);
#else
#define logging_get_verbosity() VERBOSITY_NORMAL
#endif

#endif /* defined(__caqe__logging__) */
