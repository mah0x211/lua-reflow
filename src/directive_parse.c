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

#include "directive_parse.h"
#include "ir_internal.h"
#include "json5.h"
#include "pool.h"

typedef int (*directive_match_fn)(const char *suffix, size_t suffix_len,
                                  const char *name, size_t name_len);
typedef int (*directive_parse_fn)(const directive_parse_ctx_t *ctx,
                                  const char *suffix, size_t suffix_len,
                                  const char *value, size_t value_len,
                                  directive_parse_error_t *error);

typedef struct directive_parser_t {
    const char *name;
    size_t name_len;
    directive_match_fn match;
    directive_parse_fn parse;
} directive_parser_t;

static int match_exact(const char *suffix, size_t suffix_len, const char *name,
                       size_t name_len)
{
    return suffix_len == name_len && memcmp(suffix, name, name_len) == 0;
}

static int parse_data(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    int base               = 0;
    pool_t *pool           = NULL;
    char *wrapped          = NULL;
    reflow_value_t *scopes = NULL;
    json5_error_t cause    = {0};
    int status             = -1;
    int errnum             = 0;

    (void)suffix;
    (void)suffix_len;
    if (ir_element_has_data(ctx->element)) {
        error->message = "duplicate x-data on element";
        errno          = EINVAL;
        return -1;
    }
    if (source_len > SIZE_MAX - 3) {
        error->message = "out of memory while parsing x-data";
        errno          = ENOMEM;
        return -1;
    }
    base = lua_gettop(ctx->L);

    pool = pool_new(ctx->L, 0);
    if (pool == NULL) {
        error->message = "out of memory while parsing x-data";
        return -1;
    }
    wrapped = (char *)pool_alloc(pool, source_len + 3);
    if (wrapped == NULL) {
        goto cleanup;
    }
    wrapped[0] = '{';
    if (source_len != 0) {
        memcpy(wrapped + 1, source, source_len);
    }
    wrapped[source_len + 1] = '}';
    wrapped[source_len + 2] = '\0';

    status =
        json5_parse(pool, ctx->ir, wrapped, source_len + 2, &scopes, &cause);
    if (status != 0) {
        if (errno == EINVAL && cause.message != NULL) {
            error->message  = "x-data: invalid JSON5";
            error->cause    = cause.message;
            error->position = cause.position;
        } else {
            error->message = errno == ENOMEM ?
                                 "out of memory while parsing x-data" :
                                 "failed to parse x-data";
        }
    } else if (ir_set_data(ctx->element, scopes) != 0) {
        status         = -1;
        error->message = errno == ENOMEM ?
                             "out of memory while building the intermediate "
                             "representation" :
                             "failed to build the intermediate representation";
    }

cleanup:
    errnum = errno;
    if (status != 0 && error->message == NULL) {
        error->message = errnum == ENOMEM ?
                             "out of memory while parsing x-data" :
                             "failed to parse x-data";
    }
    pool_delete(ctx->L, pool);
    lua_settop(ctx->L, base);
    errno = errnum;
    return status;
}

static const directive_parser_t parsers[] = {
    {
     .name     = "data",
     .name_len = sizeof("data") - 1,
     .match    = match_exact,
     .parse    = parse_data,
     },
};

int directive_parse_attribute(const directive_parse_ctx_t *ctx,
                              const char *suffix, size_t suffix_len,
                              const char *value, size_t value_len,
                              directive_parse_error_t *error)
{
    size_t i = 0;

    if (error != NULL) {
        *error = (directive_parse_error_t){0};
    }
    if (ctx == NULL || ctx->L == NULL || ctx->ir == NULL ||
        ctx->element == NULL || (suffix == NULL && suffix_len != 0) ||
        (value == NULL && value_len != 0) || error == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < sizeof(parsers) / sizeof(parsers[0]); i++) {
        const directive_parser_t *parser = &parsers[i];

        if (parser->match(suffix, suffix_len, parser->name, parser->name_len)) {
            return parser->parse(ctx, suffix, suffix_len, value, value_len,
                                 error);
        }
    }
    return 1;
}
