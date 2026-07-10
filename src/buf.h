/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#ifndef REFLOW_BUF_H
#define REFLOW_BUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} buf_t;

static inline int buf_init(buf_t *b)
{
    b->cap  = 256;
    b->len  = 0;
    b->data = (char *)malloc(b->cap);
    return b->data != NULL ? 0 : -1;
}

static inline int buf_ensure(buf_t *b, size_t need)
{
    if (b->len + need + 1 <= b->cap) {
        return 0;
    }
    while (b->len + need + 1 > b->cap) {
        b->cap *= 2;
    }
    char *nd = (char *)realloc(b->data, b->cap);
    if (nd == NULL) {
        return -1;
    }
    b->data = nd;
    return 0;
}

static inline int buf_put(buf_t *b, const char *data, size_t len)
{
    if (buf_ensure(b, len) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

static inline int buf_putc(buf_t *b, char c)
{
    if (buf_ensure(b, 1) != 0) {
        return -1;
    }
    b->data[b->len++] = c;
    b->data[b->len]   = '\0';
    return 0;
}

static inline int buf_printf(buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    if (buf_ensure(b, (size_t)n) != 0) {
        return -1;
    }
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

static inline void buf_free(buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

#endif /* REFLOW_BUF_H */
