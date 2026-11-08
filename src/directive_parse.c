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
#include <stdio.h>
#include <string.h>

#include <lauxlib.h>

#include "directive_parse.h"
#include "expr.h"
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

/* Decode one UTF-8 sequence at s[*pos] and advance *pos. Return -1 for an
 * invalid or truncated sequence. */
static long decode_utf8(const char *s, size_t len, size_t *pos)
{
    unsigned char lead = (unsigned char)s[*pos];
    size_t count       = 0;
    long code          = 0;
    size_t i           = 0;

    if (lead < 0x80) {
        (*pos)++;
        return (long)lead;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        count = 3;
        code  = lead & 0x07;
    } else if (lead >= 0xe0) {
        count = 2;
        code  = lead & 0x0f;
    } else if (lead >= 0xc2) {
        count = 1;
        code  = lead & 0x1f;
    } else {
        return -1;
    }
    if (count > len - *pos - 1) {
        return -1;
    }
    for (i = 1; i <= count; i++) {
        unsigned char next = (unsigned char)s[*pos + i];

        if ((next & 0xc0) != 0x80) {
            return -1;
        }
        code = (code << 6) | (long)(next & 0x3f);
    }
    *pos += count + 1;
    return code;
}

/* Report whether the value is empty after the whitespace removal performed
 * by JavaScript's String.prototype.trim(). */
static int is_blank(const char *value, size_t len)
{
    size_t pos = 0;

    while (pos < len) {
        long code = decode_utf8(value, len, &pos);

        if (!((code >= 0x09 && code <= 0x0d) || code == 0x20 || code == 0xa0 ||
              code == 0x1680 || (code >= 0x2000 && code <= 0x200a) ||
              code == 0x2028 || code == 0x2029 || code == 0x202f ||
              code == 0x205f || code == 0x3000 || code == 0xfeff)) {
            return 0;
        }
    }
    return 1;
}

/* Convert a byte offset in UTF-8 source to the UTF-16 code unit offset that
 * JavaScript reports for the same position. */
static size_t utf16_offset(const char *source, size_t pos)
{
    size_t units = 0;
    size_t i     = 0;

    for (i = 0; i < pos; i++) {
        unsigned char ch = (unsigned char)source[i];

        if ((ch & 0xc0) != 0x80) {
            units += ch >= 0xf0 ? 2 : 1;
        }
    }
    return units;
}

static void push_required_message(const directive_parse_ctx_t *ctx)
{
    luaL_Buffer buffer;

    luaL_buffinit(ctx->L, &buffer);
    luaL_addlstring(&buffer, ctx->attribute, ctx->attribute_len);
    luaL_addstring(&buffer, ": value is required");
    luaL_pushresult(&buffer);
}

static void push_parse_message(const directive_parse_ctx_t *ctx,
                               const char *source,
                               const reflow_expr_error_t *error)
{
    luaL_Buffer buffer;
    char position[32];
    const char *cursor = error->message;

    (void)snprintf(position, sizeof(position), "%zu",
                   utf16_offset(source, error->position));
    luaL_buffinit(ctx->L, &buffer);
    luaL_addlstring(&buffer, ctx->attribute, ctx->attribute_len);
    luaL_addstring(&buffer, ": expression parse error at position ");
    luaL_addstring(&buffer, position);
    luaL_addstring(&buffer, ": ");
    while (*cursor != '\0') {
        const char *mark = strstr(cursor, "%D");

        if (mark == NULL) {
            luaL_addstring(&buffer, cursor);
            break;
        }
        luaL_addlstring(&buffer, cursor, (size_t)(mark - cursor));
        luaL_addlstring(&buffer, source + error->detail, error->detail_len);
        cursor = mark + 2;
    }
    luaL_pushresult(&buffer);
}

static void push_unknown_helper_message(const directive_parse_ctx_t *ctx,
                                        const reflow_expr_t *call)
{
    luaL_Buffer buffer;
    const reflow_expr_string_t *name = &call->as.call.callee;

    luaL_buffinit(ctx->L, &buffer);
    luaL_addlstring(&buffer, ctx->attribute, ctx->attribute_len);
    luaL_addstring(&buffer, ": unknown helper \"");
    luaL_addlstring(&buffer, name->data, name->len);
    luaL_addstring(&buffer,
                   "\" \xe2\x80\x94 register it via reflow.new({ helpers = { ");
    luaL_addlstring(&buffer, name->data, name->len);
    luaL_addstring(&buffer, " = fn } })");
    luaL_pushresult(&buffer);
}

static int is_registered_helper(const directive_parse_ctx_t *ctx,
                                const reflow_expr_string_t *name)
{
    size_t i = 0;

    for (i = 0; i < ctx->helper_count; i++) {
        if (ctx->helpers[i].len == name->len &&
            memcmp(ctx->helpers[i].name, name->data, name->len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int push_node(pool_t *scratch, const reflow_expr_t ***stack,
                     size_t *depth, size_t *capacity, const reflow_expr_t *node)
{
    if (node == NULL) {
        return 0;
    }
    if (*depth == *capacity) {
        size_t next                = *capacity == 0 ? 16 : *capacity * 2;
        const reflow_expr_t **grow = NULL;

        if (next < *capacity || next > SIZE_MAX / sizeof(**stack)) {
            errno = ENOMEM;
            return -1;
        }
        grow = (const reflow_expr_t **)pool_realloc(
            scratch, (void *)(uintptr_t)*stack, next * sizeof(**stack));
        if (grow == NULL) {
            return -1;
        }
        *stack    = grow;
        *capacity = next;
    }
    (*stack)[(*depth)++] = node;
    return 0;
}

/* Find the call to an unregistered helper that appears first in the source,
 * which is the first one JavaScript's pre-order helper collection reports.
 * The tree is walked with an explicit stack. */
static int find_unknown_helper(const directive_parse_ctx_t *ctx,
                               pool_t *scratch, const reflow_expr_t *root,
                               const reflow_expr_t **unknown)
{
    const reflow_expr_t **stack = NULL;
    size_t depth                = 0;
    size_t capacity             = 0;
    int status                  = 0;

    *unknown = NULL;
    status   = push_node(scratch, &stack, &depth, &capacity, root);
    while (status == 0 && depth > 0) {
        const reflow_expr_t *node                = stack[--depth];
        const reflow_expr_t *item                = NULL;
        const reflow_expr_object_entry_t *member = NULL;

        switch (node->type) {
        case REFLOW_EXPR_LITERAL:
        case REFLOW_EXPR_DOLLAR:
        case REFLOW_EXPR_AT:
        case REFLOW_EXPR_DOT:
            break;
        case REFLOW_EXPR_MEMBER:
            status = push_node(scratch, &stack, &depth, &capacity,
                               node->as.member.object);
            break;
        case REFLOW_EXPR_UNARY:
            status = push_node(scratch, &stack, &depth, &capacity,
                               node->as.unary.argument);
            break;
        case REFLOW_EXPR_BINARY:
            status = push_node(scratch, &stack, &depth, &capacity,
                               node->as.binary.left);
            if (status == 0) {
                status = push_node(scratch, &stack, &depth, &capacity,
                                   node->as.binary.right);
            }
            break;
        case REFLOW_EXPR_TERNARY:
            status = push_node(scratch, &stack, &depth, &capacity,
                               node->as.ternary.test);
            if (status == 0) {
                status = push_node(scratch, &stack, &depth, &capacity,
                                   node->as.ternary.consequent);
            }
            if (status == 0) {
                status = push_node(scratch, &stack, &depth, &capacity,
                                   node->as.ternary.alternate);
            }
            break;
        case REFLOW_EXPR_CALL:
            if (!is_registered_helper(ctx, &node->as.call.callee) &&
                (*unknown == NULL || node->offset < (*unknown)->offset)) {
                *unknown = node;
            }
            for (item = node->as.call.arguments.head;
                 status == 0 && item != NULL; item = item->next) {
                status = push_node(scratch, &stack, &depth, &capacity, item);
            }
            break;
        case REFLOW_EXPR_OBJECT:
            for (member = node->as.object.head; status == 0 && member != NULL;
                 member = member->next) {
                status =
                    push_node(scratch, &stack, &depth, &capacity, member->key);
                if (status == 0) {
                    status = push_node(scratch, &stack, &depth, &capacity,
                                       member->value);
                }
            }
            break;
        case REFLOW_EXPR_ARRAY:
            for (item = node->as.array.head; status == 0 && item != NULL;
                 item = item->next) {
                status = push_node(scratch, &stack, &depth, &capacity, item);
            }
            break;
        }
    }
    if (stack != NULL) {
        int errnum = errno;

        (void)pool_free(scratch, (void *)(uintptr_t)stack);
        errno = errnum;
    }
    return status;
}

/* A directive whose value is one expression stored in its own IR slot. */
typedef struct expression_directive_t {
    const char *duplicate_message;
    const char *oom_message;
    int (*has)(const ir_element_t *element);
    int (*set)(ir_element_t *element, const reflow_expression_t *expression);
} expression_directive_t;

static const expression_directive_t TEXT_DIRECTIVE = {
    .duplicate_message = "duplicate x-text on element",
    .oom_message       = "out of memory while parsing x-text",
    .has               = ir_element_has_text,
    .set               = ir_set_text,
};

static const expression_directive_t HTML_DIRECTIVE = {
    .duplicate_message = "duplicate x-html on element",
    .oom_message       = "out of memory while parsing x-html",
    .has               = ir_element_has_html,
    .set               = ir_set_html,
};

static int parse_expression_directive(const directive_parse_ctx_t *ctx,
                                      const expression_directive_t *directive,
                                      const char *source, size_t source_len,
                                      directive_parse_error_t *error)
{
    lua_State *L                    = ctx->L;
    int base                        = lua_gettop(L);
    pool_t *scratch                 = NULL;
    reflow_expression_t *expression = NULL;
    reflow_expr_error_t parse_error = {0};
    const reflow_expr_t *unknown    = NULL;
    int status                      = -1;
    int errnum                      = 0;

    if (directive->has(ctx->element)) {
        error->message = directive->duplicate_message;
        errno          = EINVAL;
        return -1;
    }
    if (is_blank(source, source_len)) {
        push_required_message(ctx);
        error->message        = lua_tostring(L, -1);
        error->message_pushed = 1;
        errno                 = EINVAL;
        return -1;
    }

    scratch = pool_new(L, 0);
    if (scratch == NULL) {
        error->message = directive->oom_message;
        return -1;
    }
    status = reflow_expr_parse(ir_pool(ctx->ir), scratch, source, source_len,
                               &expression, &parse_error);
    if (status == 0) {
        status = find_unknown_helper(ctx, scratch, expression->root, &unknown);
    }
    errnum = errno;
    pool_delete(L, scratch);
    lua_settop(L, base);
    errno = errnum;

    if (status != 0) {
        if (errnum == ENOMEM) {
            error->message = directive->oom_message;
            return -1;
        }
        push_parse_message(ctx, source, &parse_error);
    } else if (unknown != NULL) {
        push_unknown_helper_message(ctx, unknown);
    } else if (directive->set(ctx->element, expression) != 0) {
        error->message = errno == ENOMEM ?
                             "out of memory while building the intermediate "
                             "representation" :
                             "failed to build the intermediate representation";
        return -1;
    } else {
        return 0;
    }
    error->message        = lua_tostring(L, -1);
    error->message_pushed = 1;
    errno                 = EINVAL;
    return -1;
}

static int parse_text(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    (void)suffix;
    (void)suffix_len;
    return parse_expression_directive(ctx, &TEXT_DIRECTIVE, source, source_len,
                                      error);
}

static int parse_html(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    (void)suffix;
    (void)suffix_len;
    return parse_expression_directive(ctx, &HTML_DIRECTIVE, source, source_len,
                                      error);
}

static const directive_parser_t parsers[] = {
    {
     .name     = "data",
     .name_len = sizeof("data") - 1,
     .match    = match_exact,
     .parse    = parse_data,
     },
    {
     .name     = "text",
     .name_len = sizeof("text") - 1,
     .match    = match_exact,
     .parse    = parse_text,
     },
    {
     .name     = "html",
     .name_len = sizeof("html") - 1,
     .match    = match_exact,
     .parse    = parse_html,
     },
};

int directive_validate_element(const directive_parse_ctx_t *ctx,
                               directive_parse_error_t *error)
{
    const ir_element_t *element = ctx->element;
    int content                 = 0;

    *error  = (directive_parse_error_t){0};
    content = ir_element_has_text(element) + ir_element_has_html(element);
    if (content > 1) {
        error->message = "conflicting content directives on same element "
                         "(x-text / x-html / x-include are mutually exclusive)";
        errno          = EINVAL;
        return -1;
    }
    return 0;
}

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
