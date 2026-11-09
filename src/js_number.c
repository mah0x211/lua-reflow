/*
 * Copyright (C) 2026 Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "js_number.h"

/* Write the shortest decimal digits that read back as a (finite, positive)
 * into digits and return their count; *exponent receives the decimal
 * exponent of the first digit. The C library must round correctly, as C99
 * requires of snprintf and strtod. */
static size_t shortest_digits(double a, char digits[18], int *exponent)
{
    char tmp[40];
    size_t count  = 0;
    const char *p = NULL;
    int precision = 0;
    double back   = 0;

    /* 17 significant digits always read back as the same double. */
    for (precision = 0; precision < 17; precision++) {
        (void)snprintf(tmp, sizeof(tmp), "%.*e", precision, a);
        back = strtod(tmp, NULL);
        if (!(back < a) && !(back > a)) {
            break;
        }
    }
    /* The decimal point follows the first digit and may be locale
     * specific, so collect the digits up to the exponent marker. */
    for (p = tmp; *p != 'e'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits[count++] = *p;
        }
    }
    *exponent = atoi(p + 1);
    return count;
}

size_t js_number_format(double n, char buf[JS_NUMBER_BUFSIZE])
{
    char digits[18] = {0};
    char *out       = buf;
    size_t count    = 0;
    int exponent    = 0;
    int point       = 0; /* digits before the decimal point */

    if (isnan(n)) {
        memcpy(buf, "NaN", 4);
        return 3;
    } else if (isinf(n)) {
        if (n < 0) {
            memcpy(buf, "-Infinity", 10);
            return 9;
        }
        memcpy(buf, "Infinity", 9);
        return 8;
    } else if (fpclassify(n) == FP_ZERO) {
        memcpy(buf, "0", 2);
        return 1;
    }

    if (n < 0) {
        *out++ = '-';
        n      = -n;
    }
    count = shortest_digits(n, digits, &exponent);
    point = exponent + 1;

    if ((int)count <= point && point <= 21) {
        /* An integer: the digits followed by zeros. */
        memcpy(out, digits, count);
        out += count;
        memset(out, '0', (size_t)point - count);
        out += (size_t)point - count;
    } else if (0 < point && point <= 21) {
        /* A fraction with an integer part. */
        memcpy(out, digits, (size_t)point);
        out += point;
        *out++ = '.';
        memcpy(out, digits + point, count - (size_t)point);
        out += count - (size_t)point;
    } else if (-6 < point && point <= 0) {
        /* A small fraction: "0." and leading zeros. */
        *out++ = '0';
        *out++ = '.';
        memset(out, '0', (size_t)-point);
        out += -point;
        memcpy(out, digits, count);
        out += count;
    } else {
        /* Exponential notation: d[.ddd]e+x or d[.ddd]e-x. */
        *out++ = digits[0];
        if (count > 1) {
            *out++ = '.';
            memcpy(out, digits + 1, count - 1);
            out += count - 1;
        }
        out += snprintf(out, (size_t)(buf + JS_NUMBER_BUFSIZE - out), "e%c%d",
                        point - 1 < 0 ? '-' : '+', abs(point - 1));
    }
    *out = '\0';
    return (size_t)(out - buf);
}
