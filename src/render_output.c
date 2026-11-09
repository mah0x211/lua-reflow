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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "render_output.h"

#define RENDER_OUTPUT_INITIAL_CAPACITY 256

static int reserve(render_output_t *out, size_t len)
{
    size_t need     = 0;
    size_t capacity = 0;
    char *data      = NULL;

    if (len <= out->capacity - out->len) {
        return 0;
    } else if (len > SIZE_MAX - out->len) {
        errno = ENOMEM;
        return -1;
    }
    need     = out->len + len;
    capacity = out->capacity ? out->capacity : RENDER_OUTPUT_INITIAL_CAPACITY;
    while (capacity < need) {
        capacity = capacity > SIZE_MAX / 2 ? need : capacity * 2;
    }
    data = (char *)pool_realloc(out->pool, out->data, capacity);
    if (data == NULL) {
        return -1;
    }
    out->data     = data;
    out->capacity = capacity;
    return 0;
}

int render_output_write(render_output_t *out, const char *s, size_t len)
{
    if (len == 0) {
        return 0;
    } else if (reserve(out, len) != 0) {
        return -1;
    }
    memcpy(out->data + out->len, s, len);
    out->len += len;
    return 0;
}

int render_output_write_attr_value(render_output_t *out, const char *s,
                                   size_t len)
{
    const char *end = s + len;

    while (s < end) {
        const char *quote = (const char *)memchr(s, '"', (size_t)(end - s));
        size_t run        = quote ? (size_t)(quote - s) : (size_t)(end - s);

        if (render_output_write(out, s, run) != 0) {
            return -1;
        }
        s += run;
        if (quote) {
            if (render_output_write(out, "&quot;", 6) != 0) {
                return -1;
            }
            s++;
        }
    }
    return 0;
}
