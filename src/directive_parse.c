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
#include "reflow_util.h"

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

static void set_error_location(directive_parse_error_t *error,
                               const char *source, size_t source_len,
                               size_t pos)
{
    error->expression     = source;
    error->expression_len = source_len;
    error->expression_pos = pos;
}

/* Push "<head>: expression parse error at position N: <reason><tail>",
 * the form the JavaScript version uses for expression errors. */
static void push_parse_message_with(lua_State *L, const char *head,
                                    size_t head_len, const char *source,
                                    const reflow_expr_error_t *error,
                                    const char *tail, size_t tail_len)
{
    luaL_Buffer buffer;
    char position[32];
    const char *cursor = error->message;

    (void)snprintf(position, sizeof(position), "%zu",
                   utf16_offset(source, error->position));
    luaL_buffinit(L, &buffer);
    luaL_addlstring(&buffer, head, head_len);
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
    luaL_addlstring(&buffer, tail, tail_len);
    luaL_pushresult(&buffer);
}

static void push_parse_message(const directive_parse_ctx_t *ctx,
                               const char *source,
                               const reflow_expr_error_t *error)
{
    push_parse_message_with(ctx->L, ctx->attribute, ctx->attribute_len, source,
                            error, "", 0);
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

static const expression_directive_t INCLUDE_DIRECTIVE = {
    .duplicate_message = "duplicate x-include on element",
    .oom_message       = "out of memory while parsing x-include",
    .has               = ir_element_has_include,
    .set               = ir_set_include,
};

#define IR_BUILD_FAILURE_MESSAGE(errnum)                                       \
    ((errnum) == ENOMEM ?                                                      \
         "out of memory while building the intermediate representation" :      \
         "failed to build the intermediate representation")

/* Mark the Lua string at the stack top as the error message. */
static int fail_with_pushed_message(lua_State *L,
                                    directive_parse_error_t *error)
{
    error->message        = lua_tostring(L, -1);
    error->message_pushed = 1;
    errno                 = EINVAL;
    return -1;
}

/* Parse one expression value and check its helper references, reporting
 * failures with the messages of the JavaScript version. */
static int parse_expression_value(const directive_parse_ctx_t *ctx,
                                  const char *oom_message, const char *source,
                                  size_t source_len,
                                  directive_parse_error_t *error,
                                  reflow_expression_t **result)
{
    lua_State *L                    = ctx->L;
    int base                        = lua_gettop(L);
    pool_t *scratch                 = NULL;
    reflow_expression_t *expression = NULL;
    reflow_expr_error_t parse_error = {0};
    const reflow_expr_t *unknown    = NULL;
    int status                      = -1;
    int errnum                      = 0;

    if (reflow_js_is_blank(source, source_len)) {
        push_required_message(ctx);
        return fail_with_pushed_message(L, error);
    }

    scratch = pool_new(L, 0);
    if (scratch == NULL) {
        error->message = oom_message;
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
            error->message = oom_message;
            return -1;
        }
        push_parse_message(ctx, source, &parse_error);
        set_error_location(error, source, source_len, parse_error.position);
        return fail_with_pushed_message(L, error);
    } else if (unknown != NULL) {
        push_unknown_helper_message(ctx, unknown);
        set_error_location(error, source, source_len, unknown->offset);
        return fail_with_pushed_message(L, error);
    }
    *result = expression;
    return 0;
}

static const expression_directive_t IF_DIRECTIVE = {
    .duplicate_message = "duplicate x-if on element",
    .oom_message       = "out of memory while parsing x-if",
    .has               = ir_element_has_if,
    .set               = ir_set_if,
};

static const expression_directive_t ELSEIF_DIRECTIVE = {
    .duplicate_message = "duplicate x-elseif on element",
    .oom_message       = "out of memory while parsing x-elseif",
    .has               = ir_element_has_elseif,
    .set               = ir_set_elseif,
};

static int parse_expression_directive(const directive_parse_ctx_t *ctx,
                                      const expression_directive_t *directive,
                                      const char *source, size_t source_len,
                                      directive_parse_error_t *error)
{
    reflow_expression_t *expression = NULL;

    if (directive->has(ctx->element)) {
        error->message = directive->duplicate_message;
        errno          = EINVAL;
        return -1;
    }
    if (parse_expression_value(ctx, directive->oom_message, source, source_len,
                               error, &expression) != 0) {
        return -1;
    }
    if (directive->set(ctx->element, expression) != 0) {
        error->message = IR_BUILD_FAILURE_MESSAGE(errno);
        return -1;
    }
    return 0;
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

static int parse_if(const directive_parse_ctx_t *ctx, const char *suffix,
                    size_t suffix_len, const char *source, size_t source_len,
                    directive_parse_error_t *error)
{
    (void)suffix;
    (void)suffix_len;
    return parse_expression_directive(ctx, &IF_DIRECTIVE, source, source_len,
                                      error);
}

static int parse_elseif(const directive_parse_ctx_t *ctx, const char *suffix,
                        size_t suffix_len, const char *source,
                        size_t source_len, directive_parse_error_t *error)
{
    (void)suffix;
    (void)suffix_len;
    return parse_expression_directive(ctx, &ELSEIF_DIRECTIVE, source,
                                      source_len, error);
}

/* A marker directive takes no value; only an empty or blank value is
 * accepted, as in the JavaScript version. */
static int parse_else(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    lua_State *L = ctx->L;

    (void)suffix;
    (void)suffix_len;
    if (ir_element_has_else(ctx->element)) {
        error->message = "duplicate x-else on element";
        errno          = EINVAL;
        return -1;
    }
    if (!reflow_js_is_blank(source, source_len)) {
        lua_pushlstring(L, ctx->attribute, ctx->attribute_len);
        lua_pushliteral(L, ": must not have a value; got \"");
        lua_pushlstring(L, ctx->attribute, ctx->attribute_len);
        lua_pushliteral(L, "=\"");
        lua_pushlstring(L, source, source_len);
        lua_pushliteral(L, "\"\"");
        lua_concat(L, 6);
        return fail_with_pushed_message(L, error);
    }
    if (ir_set_else(ctx->element) != 0) {
        error->message = IR_BUILD_FAILURE_MESSAGE(errno);
        return -1;
    }
    return 0;
}

static int parse_include(const directive_parse_ctx_t *ctx, const char *suffix,
                         size_t suffix_len, const char *source,
                         size_t source_len, directive_parse_error_t *error)
{
    (void)suffix;
    (void)suffix_len;
    return parse_expression_directive(ctx, &INCLUDE_DIRECTIVE, source,
                                      source_len, error);
}

#define WITH_OOM_MESSAGE "out of memory while parsing x-with"

static int is_with_name_start(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
           ch == '$';
}

static int is_with_name_char(unsigned char ch)
{
    return is_with_name_start(ch) || (ch >= '0' && ch <= '9');
}

static size_t skip_with_space(const char *s, size_t len, size_t pos)
{
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
                         s[pos] == '\r')) {
        pos++;
    }
    return pos;
}

/* Push "x-with: <before>"<name>"". */
static void push_with_name_message(lua_State *L, const char *before,
                                   const char *name, size_t name_len)
{
    lua_pushliteral(L, "x-with: ");
    lua_pushstring(L, before);
    lua_pushliteral(L, "\"");
    lua_pushlstring(L, name, name_len);
    lua_pushliteral(L, "\"");
    lua_concat(L, 5);
}

static void release_scratch(lua_State *L, pool_t *scratch, int base)
{
    int errnum = errno;

    pool_delete(L, scratch);
    lua_settop(L, base);
    errno = errnum;
}

/* Parse "name = expression, ..." with the scanner of the JavaScript version:
 * a binding value ends at the first comma outside brackets and string
 * literals. Every binding is parsed before any helper reference is checked,
 * as in the JavaScript version. */
static int parse_with(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    lua_State *L          = ctx->L;
    int base              = lua_gettop(L);
    pool_t *scratch       = NULL;
    size_t pos            = 0;
    size_t count          = 0;
    const ir_with_t *with = NULL;
    char position[32];

    (void)suffix;
    (void)suffix_len;
    if (ir_element_has_with(ctx->element, NULL, 0)) {
        error->message = "duplicate x-with on element";
        errno          = EINVAL;
        return -1;
    }
    scratch = pool_new(L, 0);
    if (scratch == NULL) {
        error->message = WITH_OOM_MESSAGE;
        return -1;
    }

    while (1) {
        size_t name_start               = 0;
        size_t name_len                 = 0;
        size_t value_start              = 0;
        size_t value_end                = 0;
        size_t depth                    = 0;
        reflow_expression_t *expression = NULL;
        reflow_expr_error_t parse_error = {0};

        pos = skip_with_space(source, source_len, pos);
        if (pos >= source_len) {
            break;
        }
        name_start = pos;
        if (!is_with_name_start((unsigned char)source[pos])) {
            release_scratch(L, scratch, base);
            (void)snprintf(position, sizeof(position), "%zu",
                           utf16_offset(source, pos));
            lua_pushliteral(L, "x-with: expected binding name at position ");
            lua_pushstring(L, position);
            lua_concat(L, 2);
            set_error_location(error, source, source_len, pos);
            return fail_with_pushed_message(L, error);
        }
        pos++;
        while (pos < source_len &&
               is_with_name_char((unsigned char)source[pos])) {
            pos++;
        }
        name_len = pos - name_start;
        if (ir_element_has_with(ctx->element, source + name_start, name_len)) {
            release_scratch(L, scratch, base);
            push_with_name_message(L, "duplicate binding name ",
                                   source + name_start, name_len);
            set_error_location(error, source, source_len, name_start);
            return fail_with_pushed_message(L, error);
        }
        pos = skip_with_space(source, source_len, pos);
        if (pos >= source_len || source[pos] != '=') {
            release_scratch(L, scratch, base);
            push_with_name_message(L, "expected \"=\" after binding name ",
                                   source + name_start, name_len);
            set_error_location(error, source, source_len, pos);
            return fail_with_pushed_message(L, error);
        }
        pos++;

        pos         = skip_with_space(source, source_len, pos);
        value_start = pos;
        while (pos < source_len) {
            char ch = source[pos];

            if (ch == '"' || ch == '\'') {
                size_t quote = pos++;

                while (pos < source_len && source[pos] != ch) {
                    pos += source[pos] == '\\' && pos + 1 < source_len ? 2 : 1;
                }
                if (pos >= source_len) {
                    release_scratch(L, scratch, base);
                    push_with_name_message(
                        L, "unterminated string literal in binding ",
                        source + name_start, name_len);
                    set_error_location(error, source, source_len, quote);
                    return fail_with_pushed_message(L, error);
                }
                pos++;
                continue;
            }
            if (ch == '{' || ch == '[' || ch == '(') {
                depth++;
            } else if (ch == '}' || ch == ']' || ch == ')') {
                if (depth == 0) {
                    release_scratch(L, scratch, base);
                    lua_pushliteral(L, "x-with: unbalanced \"");
                    lua_pushlstring(L, &source[pos], 1);
                    lua_pushliteral(L, "\" in binding \"");
                    lua_pushlstring(L, source + name_start, name_len);
                    lua_pushliteral(L, "\"");
                    lua_concat(L, 5);
                    set_error_location(error, source, source_len, pos);
                    return fail_with_pushed_message(L, error);
                }
                depth--;
            } else if (ch == ',' && depth == 0) {
                break;
            }
            pos++;
        }
        if (depth != 0) {
            release_scratch(L, scratch, base);
            push_with_name_message(L, "unbalanced brackets in binding ",
                                   source + name_start, name_len);
            set_error_location(error, source, source_len, pos);
            return fail_with_pushed_message(L, error);
        }
        value_end = pos;
        reflow_js_trim(source, &value_start, &value_end);
        if (value_start == value_end) {
            release_scratch(L, scratch, base);
            push_with_name_message(L,
                                   "value expression is required for binding ",
                                   source + name_start, name_len);
            set_error_location(error, source, source_len, value_start);
            return fail_with_pushed_message(L, error);
        }
        if (reflow_expr_parse(ir_pool(ctx->ir), scratch, source + value_start,
                              value_end - value_start, &expression,
                              &parse_error) != 0) {
            release_scratch(L, scratch, base);
            if (errno == ENOMEM) {
                error->message = WITH_OOM_MESSAGE;
                return -1;
            }
            push_parse_message_with(L, "x-with", sizeof("x-with") - 1,
                                    source + value_start, &parse_error, "", 0);
            lua_pushliteral(L, " (in binding \"");
            lua_pushlstring(L, source + name_start, name_len);
            lua_pushliteral(L, "\")");
            lua_concat(L, 4);
            set_error_location(error, source + value_start,
                               value_end - value_start, parse_error.position);
            return fail_with_pushed_message(L, error);
        }
        if (ir_append_with(ctx->element, source + name_start, name_len,
                           expression) != 0) {
            release_scratch(L, scratch, base);
            error->message = IR_BUILD_FAILURE_MESSAGE(errno);
            return -1;
        }
        count++;

        pos = skip_with_space(source, source_len, pos);
        if (pos >= source_len) {
            break;
        }
        pos++;
    }

    if (count == 0) {
        release_scratch(L, scratch, base);
        error->message = "x-with: at least one binding is required";
        errno          = EINVAL;
        return -1;
    }
    for (with = ctx->element->directives->with.head; with != NULL;
         with = with->next) {
        const reflow_expr_t *unknown = NULL;

        if (find_unknown_helper(ctx, scratch, with->expression->root,
                                &unknown) != 0) {
            release_scratch(L, scratch, base);
            error->message = WITH_OOM_MESSAGE;
            return -1;
        }
        if (unknown != NULL) {
            release_scratch(L, scratch, base);
            push_unknown_helper_message(ctx, unknown);
            set_error_location(error, with->expression->source.data,
                               with->expression->source.len, unknown->offset);
            return fail_with_pushed_message(L, error);
        }
    }
    release_scratch(L, scratch, base);
    return 0;
}

#define BIND_PREFIX     "bind:"
#define BIND_PREFIX_LEN (sizeof(BIND_PREFIX) - 1)

static int match_bind(const char *suffix, size_t suffix_len, const char *name,
                      size_t name_len)
{
    return suffix_len >= name_len && memcmp(suffix, name, name_len) == 0;
}

static int parse_bind(const directive_parse_ctx_t *ctx, const char *suffix,
                      size_t suffix_len, const char *source, size_t source_len,
                      directive_parse_error_t *error)
{
    lua_State *L                    = ctx->L;
    const char *target              = suffix + BIND_PREFIX_LEN;
    size_t target_len               = suffix_len - BIND_PREFIX_LEN;
    reflow_expression_t *expression = NULL;
    luaL_Buffer buffer;

    if (target_len == 0) {
        luaL_buffinit(L, &buffer);
        luaL_addlstring(&buffer, ctx->attribute, ctx->attribute_len);
        luaL_addstring(&buffer, ": attribute name after \"bind:\" is required");
        luaL_pushresult(&buffer);
        return fail_with_pushed_message(L, error);
    }
    if (ir_element_has_bind(ctx->element, target, target_len)) {
        luaL_buffinit(L, &buffer);
        luaL_addstring(&buffer, "duplicate x-bind:");
        luaL_addlstring(&buffer, target, target_len);
        luaL_addstring(&buffer, " on element");
        luaL_pushresult(&buffer);
        return fail_with_pushed_message(L, error);
    }
    if (parse_expression_value(ctx, "out of memory while parsing x-bind",
                               source, source_len, error, &expression) != 0) {
        return -1;
    }
    if (ir_append_bind(ctx->element, target, target_len, expression) != 0) {
        error->message = IR_BUILD_FAILURE_MESSAGE(errno);
        return -1;
    }
    return 0;
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
    {
     .name     = "include",
     .name_len = sizeof("include") - 1,
     .match    = match_exact,
     .parse    = parse_include,
     },
    {
     .name     = "if",
     .name_len = sizeof("if") - 1,
     .match    = match_exact,
     .parse    = parse_if,
     },
    {
     .name     = "elseif",
     .name_len = sizeof("elseif") - 1,
     .match    = match_exact,
     .parse    = parse_elseif,
     },
    {
     .name     = "else",
     .name_len = sizeof("else") - 1,
     .match    = match_exact,
     .parse    = parse_else,
     },
    {
     .name     = "with",
     .name_len = sizeof("with") - 1,
     .match    = match_exact,
     .parse    = parse_with,
     },
    {
     .name     = BIND_PREFIX,
     .name_len = BIND_PREFIX_LEN,
     .match    = match_bind,
     .parse    = parse_bind,
     },
};

int directive_validate_element(const directive_parse_ctx_t *ctx,
                               directive_parse_error_t *error)
{
    const ir_element_t *element = ctx->element;
    int content                 = 0;

    *error = (directive_parse_error_t){0};
    if (ir_element_has_if(element) + ir_element_has_elseif(element) +
            ir_element_has_else(element) >
        1) {
        error->message =
            "conflicting structural directives on same element "
            "(x-if / x-elseif / x-else / x-match / x-case / x-nocase are "
            "mutually exclusive)";
        errno = EINVAL;
        return -1;
    }
    content = ir_element_has_text(element) + ir_element_has_html(element) +
              ir_element_has_include(element);
    if (content > 1) {
        error->message = "conflicting content directives on same element "
                         "(x-text / x-html / x-include are mutually exclusive)";
        errno          = EINVAL;
        return -1;
    }
    if (element->directives != NULL &&
        element->directives->data_scopes != NULL) {
        const ir_with_t *with = NULL;

        for (with = element->directives->with.head; with != NULL;
             with = with->next) {
            const reflow_value_member_t *member = NULL;

            for (member = element->directives->data_scopes->as.object.head;
                 member != NULL; member = member->next) {
                if (member->key.len == with->name.len &&
                    memcmp(member->key.data, with->name.data, with->name.len) ==
                        0) {
                    lua_pushliteral(ctx->L, "name \"");
                    lua_pushlstring(ctx->L, with->name.data, with->name.len);
                    lua_pushliteral(ctx->L, "\" declared by both x-data and "
                                            "x-with on the same element");
                    lua_concat(ctx->L, 3);
                    return fail_with_pushed_message(ctx->L, error);
                }
            }
        }
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
