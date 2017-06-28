//
//  logging.c
//  caqe
//
//  Copyright (c) 2015, Leander Tentrup, Saarland University
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include "logging.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define print_arguments(format, type) \
    va_list arg;\
    va_start(arg, format);\
    fprintf(stdout, type);\
    vfprintf(stdout, format, arg);\
    va_end(arg);

#define print_arguments_no_type(format) \
    va_list arg;\
    va_start(arg, format);\
    vfprintf(stdout, format, arg);\
    va_end(arg);

verbosity logging_verbosity = VERBOSITY_NORMAL;

#ifdef LOGGING

void logging_info(const char* format, ...) {
    if (logging_verbosity >= VERBOSITY_HIGH) {
        print_arguments_no_type(format);
    }
}

void logging_debug(const char* format, ...) {
    if (logging_verbosity >= VERBOSITY_ALL) {
        print_arguments_no_type(format);
    }
}

#endif

void logging_fatal(const char* format, ...) {
    print_arguments(format, "FATAL ERROR: ");
    abort();
}

void logging_error(const char* format, ...) {
    print_arguments(format, "error: ");
}

void logging_warn(const char* format, ...) {
    if (logging_verbosity >= VERBOSITY_NORMAL) {
        print_arguments(format, "warning: ");
    }
}

void logging_print(const char* format, ...) {
    if (logging_verbosity >= VERBOSITY_NORMAL) {
        print_arguments_no_type(format);
    }
}

#ifndef NO_API_ASSERTIONS
void api_expect(bool assertion, const char* format, ...) {
    if (!assertion) {
        print_arguments(format, "API error: ");
        abort();
    }
}
#endif

#ifndef NDEBUG
void fixme(const char* format, ...) {
    print_arguments(format, "fixme: ");
}
#endif

void logging_set_verbosity(verbosity v) {
    logging_verbosity = v;
}

#ifdef LOGGING
verbosity logging_get_verbosity() {
    return logging_verbosity;
}
#endif
