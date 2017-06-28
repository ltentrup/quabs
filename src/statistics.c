//
//  statistics.c
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

#include "statistics.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include "util.h"

double get_seconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) (tv.tv_usec) / 1000000 + (double) (tv.tv_sec);
}

Stats* statistics_init(double factor) {
    Stats* s = malloc(sizeof(Stats));
    s->calls_num = 0;
    s->accumulated_value = 0.0;
    s->max = 0.0;
    s->min = 0.0;
    s->exponential_histogram = int_vector_init();
    s->factor = factor;
    s->time_stamp = 0.0;
    return s;
}

void statistics_free(Stats* s) {
    int_vector_free(s->exponential_histogram);
    free(s);
}

void statistics_start_timer(Stats* s) {
    s->time_stamp = get_seconds();
}

void statistics_stop_and_record_timer(Stats* s) {
    double diff = get_seconds() - s->time_stamp;
    statistic_add_value(s, diff);
}

void statistic_add_value(Stats* s, double v) {
    assert(v >= 0.0 && v < 1000000000.0);
    // init
    if (s->calls_num == 0) {
        s->max = v;
        s->min = v;
    }
    // normal case:
    s->calls_num += 1;
    s->accumulated_value += v;
    if (v > s->max) {
        s->max = v;
    }
    if (v < s->min) {
        s->min = v;
    }
    long hist_value = lround(ceil(log2(s->factor * v))); // resolution is 0.1ms
    if (hist_value < 0) {
        hist_value = 0;
    }
    assert(hist_value < 100);
    while (((size_t) hist_value) >= int_vector_count(s->exponential_histogram)) {
        int_vector_add(s->exponential_histogram, 0);
    }
    int cur_value = int_vector_get(s->exponential_histogram, (int) hist_value);
    int_vector_set(s->exponential_histogram, (int) hist_value, cur_value + 1);
}

void statistics_print_time(Stats* s) {
    printf("%f\n", s->accumulated_value);
}

void statistics_print(Stats* s) {
    printf("    Average: %f\n    Total  : %f\n    Min/Max: %f/%f\n    Count  : %d\n",
           s->accumulated_value/s->calls_num,
           s->accumulated_value,
           s->min,
           s->max,
           s->calls_num);
    printf("    Histogram:");

    for (size_t i = 0; i < int_vector_count(s->exponential_histogram); i += 1) {
        if ( s->factor == 1) {
            printf("  %ld:", lround(floor(exp2(i))));
        }
        if ( s->factor == 10000 && i==0) {printf(" 0.1ms:");}
        if ( s->factor == 10000 && i==3) {printf("; 0.8ms:");}
        if ( s->factor == 10000 && i==6) {printf("; 6.4ms:");}
        if ( s->factor == 10000 && i==9) {printf("; 51.2ms:");}
        if ( s->factor == 10000 && i==12) {printf("; 409ms:");}
        if ( s->factor == 10000 && i==15) {printf("; 3.27s:");}
        printf(" %d",int_vector_get(s->exponential_histogram, i));
    }
    printf("\n");
}
