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

#ifndef REFLOW_JS_NUMBER_H
#define REFLOW_JS_NUMBER_H

#include <stddef.h>

/* Large enough for any result of js_number_format, including the
 * terminating NUL: at most 17 significant digits, a sign, a decimal point,
 * and either up to 20 padding zeros or a five-character exponent. */
#define JS_NUMBER_BUFSIZE 32

/**
 * Format a number the way JavaScript's String(n) does: the shortest digit
 * sequence that reads back as the same double, in fixed notation for
 * decimal exponents from -6 to 20 and in exponential notation otherwise.
 * NaN, Infinity, and -Infinity are spelled as in JavaScript, and -0 is "0".
 *
 * @return the length of the NUL-terminated result written to buf.
 */
size_t js_number_format(double n, char buf[JS_NUMBER_BUFSIZE]);

#endif /* REFLOW_JS_NUMBER_H */
