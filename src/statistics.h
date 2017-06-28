//
//  statistics.h
//  caqe
//
//  Copyright (c) 2015, Markus Rabe, Saarland University.
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

#ifndef __caqe__statistics__
#define __caqe__statistics__

#include <stdio.h>
#include "vector.h"

typedef struct {
    int calls_num;
    double accumulated_value;
    double max;
    double min;
    int_vector* exponential_histogram; // stores the histogram of the logarithm of the microseconds.
    double factor; // factor applied to value before adding it to histogram
    double time_stamp;
} Stats;

Stats* statistics_init(double factor);
void statistics_free(Stats*);
void statistic_add_value(Stats* s, double v);
void statistics_print(Stats* s);
// For using it as a timer:
void statistics_start_timer(Stats* s);
void statistics_stop_and_record_timer(Stats* s);
void statistics_print_time(Stats* s);

#endif /* defined(__caqe__statistics__) */
