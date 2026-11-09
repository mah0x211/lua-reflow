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

#ifndef REFLOW_RENDER_OUTPUT_H
#define REFLOW_RENDER_OUTPUT_H

#include <stddef.h>

#include "pool.h"

/* A growable byte buffer in a render's temporary pool. The buffer does not
 * touch the Lua stack, so a render may keep Lua values on the stack while it
 * writes. */
typedef struct render_output_t {
    pool_t *pool;
    char *data;
    size_t len;
    size_t capacity;
} render_output_t;

/**
 * Append len bytes.
 *
 * @return 0 on success, or -1 with errno set to ENOMEM; the buffer is left
 *         unchanged on failure.
 */
int render_output_write(render_output_t *out, const char *s, size_t len);

/**
 * Append a source attribute value for a double-quoted attribute. The value is
 * written as it appears in the source except that '"' becomes "&quot;", so a
 * browser decodes the same value whichever quotes the source used.
 *
 * @return 0 on success, or -1 with errno set to ENOMEM.
 */
int render_output_write_attr_value(render_output_t *out, const char *s,
                                   size_t len);

#endif /* REFLOW_RENDER_OUTPUT_H */
