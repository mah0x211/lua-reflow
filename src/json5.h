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

#ifndef REFLOW_JSON5_H
#define REFLOW_JSON5_H

#include <stddef.h>

#include "ir.h"
#include "pool.h"
#include "value.h"

typedef struct json5_error_t {
    const char *message;
    size_t position;
} json5_error_t;

/**
 * Parse one complete JSON5 value and copy it into the result owner's pool.
 *
 * temporary_pool owns every yyjson allocation and the iterative traversal
 * stack. The yyjson document is released before this function returns. On
 * success, result points only into the ir result pool.
 *
 * @return 0 on success, or -1 with errno set to EINVAL for invalid JSON5,
 *         ENOMEM for allocation failure, or EIO for an unexpected value type.
 */
int json5_parse(pool_t *temporary_pool, ir_t *ir, char *source,
                size_t source_len, reflow_value_t **result,
                json5_error_t *error);

#endif /* REFLOW_JSON5_H */
