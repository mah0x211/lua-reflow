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
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "pool.h"
#include "value.h"

#define COMPUTED_KEY_MESSAGE                                                   \
    "computed object key must be a string, number, or scope reference "        \
    "(helpers and operators are not allowed here)"

typedef enum expr_frame_kind_t {
    EXPR_FRAME_TERNARY = 1,
    EXPR_FRAME_COALESCE,
    EXPR_FRAME_OR,
    EXPR_FRAME_AND,
    EXPR_FRAME_COMPARISON,
    EXPR_FRAME_UNARY,
    EXPR_FRAME_POSTFIX,
    EXPR_FRAME_PRIMARY
} expr_frame_kind_t;

typedef enum primary_state_t {
    PRIMARY_START = 0,
    PRIMARY_GROUP,
    PRIMARY_ARRAY_VALUE,
    PRIMARY_CALL_ARGUMENT,
    PRIMARY_OBJECT_VALUE
} primary_state_t;

typedef struct expr_frame_t {
    expr_frame_kind_t kind;
    unsigned int state;
    size_t start;
    reflow_expr_t *first;
    reflow_expr_t *second;
    reflow_expr_t *key;
    reflow_expr_operator_t op;
    reflow_expr_string_t name;
    reflow_expr_list_t list;
    reflow_expr_object_t object;
    int computed;
} expr_frame_t;

typedef struct expr_parser_t {
    pool_t *result_pool;
    pool_t *temporary;
    const char *source;
    size_t source_len;
    size_t pos;
    expr_frame_t *frames;
    size_t depth;
    size_t capacity;
    reflow_expr_t *last;
    reflow_expr_error_t *error;
} expr_parser_t;

static int is_space(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int is_digit(unsigned char ch)
{
    return ch >= '0' && ch <= '9';
}

static int is_identifier_start(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
           ch == '$';
}

static int is_identifier_continue(unsigned char ch)
{
    return is_identifier_start(ch) || is_digit(ch);
}

static void skip_space(expr_parser_t *parser)
{
    while (parser->pos < parser->source_len &&
           is_space((unsigned char)parser->source[parser->pos])) {
        parser->pos++;
    }
}

static int fail_parse_detail(expr_parser_t *parser, const char *message,
                             size_t detail, size_t detail_len)
{
    parser->error->kind       = REFLOW_EXPR_ERROR_PARSE;
    parser->error->message    = message;
    parser->error->position   = parser->pos;
    parser->error->detail     = detail;
    parser->error->detail_len = detail_len;
    errno                     = EINVAL;
    return -1;
}

static int fail_parse(expr_parser_t *parser, const char *message)
{
    return fail_parse_detail(parser, message, 0, 0);
}

/* Return the byte length of the UTF-8 sequence that starts at pos, bounded by
 * the source length, so that a diagnostic can quote one whole character. */
static size_t character_length(const expr_parser_t *parser, size_t pos)
{
    unsigned char lead = (unsigned char)parser->source[pos];
    size_t len         = 1;

    if (lead >= 0xf0) {
        len = 4;
    } else if (lead >= 0xe0) {
        len = 3;
    } else if (lead >= 0xc0) {
        len = 2;
    }
    if (len > parser->source_len - pos) {
        len = parser->source_len - pos;
    }
    return len;
}

static int fail_oom(expr_parser_t *parser)
{
    parser->error->kind       = REFLOW_EXPR_ERROR_OOM;
    parser->error->message    = "out of memory";
    parser->error->position   = parser->pos;
    parser->error->detail     = 0;
    parser->error->detail_len = 0;
    errno                     = ENOMEM;
    return -1;
}

static void release_temporary(expr_parser_t *parser, void *memory)
{
    int errnum = errno;

    (void)pool_free(parser->temporary, memory);
    errno = errnum;
}

static int starts_with(expr_parser_t *parser, const char *text, size_t len)
{
    skip_space(parser);
    return len <= parser->source_len - parser->pos &&
           memcmp(parser->source + parser->pos, text, len) == 0;
}

static int consume(expr_parser_t *parser, const char *text, size_t len)
{
    if (!starts_with(parser, text, len)) {
        return 0;
    }
    parser->pos += len;
    return 1;
}

static int expect(expr_parser_t *parser, char ch, const char *message)
{
    skip_space(parser);
    if (parser->pos >= parser->source_len ||
        parser->source[parser->pos] != ch) {
        return fail_parse(parser, message);
    }
    parser->pos++;
    return 0;
}

static int read_identifier(expr_parser_t *parser, size_t *start, size_t *len)
{
    size_t begin = parser->pos;

    if (begin >= parser->source_len ||
        !is_identifier_start((unsigned char)parser->source[begin])) {
        return 0;
    }
    parser->pos++;
    while (parser->pos < parser->source_len &&
           is_identifier_continue((unsigned char)parser->source[parser->pos])) {
        parser->pos++;
    }
    *start = begin;
    *len   = parser->pos - begin;
    return 1;
}

static int copy_string(expr_parser_t *parser, const char *data, size_t len,
                       reflow_expr_string_t *result)
{
    pool_t *pool = parser->result_pool;
    char *copy   = NULL;

    if (pool == NULL) {
        return -1;
    } else if (len == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }
    copy = (char *)pool_alloc(pool, len + 1);
    if (copy == NULL) {
        return -1;
    }
    if (len != 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    *result   = (reflow_expr_string_t){
        .data = copy,
        .len  = len,
    };
    return 0;
}

static reflow_value_t *new_value(expr_parser_t *parser,
                                 reflow_value_type_t type)
{
    reflow_value_t *value =
        (reflow_value_t *)pool_calloc(parser->result_pool, 1, sizeof(*value));

    if (value != NULL) {
        value->type = type;
    }
    return value;
}

static reflow_value_t *new_bool_value(expr_parser_t *parser, int boolean)
{
    reflow_value_t *value = new_value(parser, REFLOW_VALUE_BOOL);

    if (value != NULL) {
        value->as.boolean = boolean != 0;
    }
    return value;
}

static reflow_value_t *new_number_value(expr_parser_t *parser, double number)
{
    reflow_value_t *value = new_value(parser, REFLOW_VALUE_NUMBER);

    if (value != NULL) {
        value->as.number = number;
    }
    return value;
}

static reflow_value_t *new_string_value(expr_parser_t *parser, const char *data,
                                        size_t len)
{
    reflow_value_t *value = new_value(parser, REFLOW_VALUE_STRING);
    char *copy            = NULL;

    if (value == NULL) {
        return NULL;
    }
    if (len == SIZE_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    copy = (char *)pool_alloc(parser->result_pool, len + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (len != 0) {
        memcpy(copy, data, len);
    }
    copy[len]             = '\0';
    value->as.string.data = copy;
    value->as.string.len  = len;
    return value;
}

static reflow_expr_t *new_expr(expr_parser_t *parser, reflow_expr_type_t type,
                               size_t start, size_t end)
{
    pool_t *pool         = parser->result_pool;
    reflow_expr_t *value = NULL;

    if (pool == NULL) {
        return NULL;
    }
    value = (reflow_expr_t *)pool_calloc(pool, 1, sizeof(*value));
    if (value != NULL) {
        value->type   = type;
        value->offset = start;
        value->length = end - start;
    }
    return value;
}

static reflow_expr_t *new_literal(expr_parser_t *parser,
                                  reflow_value_t *literal, size_t start,
                                  size_t end)
{
    reflow_expr_t *node = NULL;

    if (literal == NULL) {
        return NULL;
    }
    node = new_expr(parser, REFLOW_EXPR_LITERAL, start, end);
    if (node != NULL) {
        node->as.literal = literal;
    }
    return node;
}

static reflow_expr_t *new_member(expr_parser_t *parser, reflow_expr_t *object,
                                 const char *property, size_t property_len,
                                 int optional, size_t start, size_t end)
{
    reflow_expr_string_t property_copy = {0};
    reflow_expr_t *node                = NULL;

    if (copy_string(parser, property, property_len, &property_copy) != 0) {
        return NULL;
    }
    node = new_expr(parser, REFLOW_EXPR_MEMBER, start, end);
    if (node != NULL) {
        node->as.member.object   = object;
        node->as.member.property = property_copy;
        node->as.member.optional = optional;
    }
    return node;
}

static reflow_expr_t *new_unary(expr_parser_t *parser,
                                reflow_expr_operator_t op,
                                reflow_expr_t *argument, size_t start,
                                size_t end)
{
    reflow_expr_t *node = new_expr(parser, REFLOW_EXPR_UNARY, start, end);

    if (node != NULL) {
        node->as.unary.op       = op;
        node->as.unary.argument = argument;
    }
    return node;
}

static reflow_expr_t *new_binary(expr_parser_t *parser,
                                 reflow_expr_operator_t op, reflow_expr_t *left,
                                 reflow_expr_t *right, size_t start, size_t end)
{
    reflow_expr_t *node = new_expr(parser, REFLOW_EXPR_BINARY, start, end);

    if (node != NULL) {
        node->as.binary.op    = op;
        node->as.binary.left  = left;
        node->as.binary.right = right;
    }
    return node;
}

static reflow_expr_t *new_ternary(expr_parser_t *parser, reflow_expr_t *test,
                                  reflow_expr_t *consequent,
                                  reflow_expr_t *alternate, size_t start,
                                  size_t end)
{
    reflow_expr_t *node = new_expr(parser, REFLOW_EXPR_TERNARY, start, end);

    if (node != NULL) {
        node->as.ternary.test       = test;
        node->as.ternary.consequent = consequent;
        node->as.ternary.alternate  = alternate;
    }
    return node;
}

static int append_list(expr_parser_t *parser, reflow_expr_list_t *list,
                       reflow_expr_t *node)
{
    if (list->count == SIZE_MAX) {
        return fail_oom(parser);
    }
    if (list->tail == NULL) {
        list->head = node;
    } else {
        list->tail->next = node;
    }
    list->tail = node;
    list->count++;
    return 0;
}

static int append_object_entry(expr_parser_t *parser,
                               reflow_expr_object_t *object, reflow_expr_t *key,
                               reflow_expr_t *value, int computed)
{
    pool_t *pool                      = parser->result_pool;
    reflow_expr_object_entry_t *entry = NULL;

    if (pool == NULL) {
        return -1;
    } else if (object->count == SIZE_MAX) {
        return fail_oom(parser);
    }
    entry = (reflow_expr_object_entry_t *)pool_calloc(pool, 1, sizeof(*entry));
    if (entry == NULL) {
        return fail_oom(parser);
    }
    entry->key      = key;
    entry->value    = value;
    entry->computed = computed;
    if (object->tail == NULL) {
        object->head = entry;
    } else {
        object->tail->next = entry;
    }
    object->tail = entry;
    object->count++;
    return 0;
}

static int push_frame(expr_parser_t *parser, expr_frame_kind_t kind)
{
    expr_frame_t *frames = NULL;
    size_t capacity      = parser->capacity;

    if (parser->depth == capacity) {
        if (capacity == 0) {
            capacity = 16;
        } else if (capacity > SIZE_MAX / 2) {
            return fail_oom(parser);
        } else {
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(*frames)) {
            return fail_oom(parser);
        }
        frames = (expr_frame_t *)pool_realloc(parser->temporary, parser->frames,
                                              capacity * sizeof(*frames));
        if (frames == NULL) {
            return fail_oom(parser);
        }
        parser->frames   = frames;
        parser->capacity = capacity;
    }
    parser->frames[parser->depth++] = (expr_frame_t){
        .kind = kind,
    };
    return 0;
}

static void complete_frame(expr_parser_t *parser, reflow_expr_t *value)
{
    parser->last = value;
    parser->depth--;
}

static reflow_expr_t *read_string_literal(expr_parser_t *parser)
{
    size_t start        = parser->pos;
    unsigned char quote = (unsigned char)parser->source[parser->pos++];
    size_t capacity     = parser->source_len - parser->pos;
    char *decoded       = NULL;
    size_t decoded_len  = 0;

    if (capacity == SIZE_MAX) {
        fail_oom(parser);
        return NULL;
    }
    decoded = (char *)pool_alloc(parser->temporary, capacity + 1);
    if (decoded == NULL) {
        fail_oom(parser);
        return NULL;
    }
    while (parser->pos < parser->source_len) {
        unsigned char ch = (unsigned char)parser->source[parser->pos++];

        if (ch == quote) {
            reflow_value_t *value =
                new_string_value(parser, decoded, decoded_len);
            reflow_expr_t *node =
                new_literal(parser, value, start, parser->pos);

            release_temporary(parser, decoded);
            if (node == NULL) {
                fail_oom(parser);
            }
            return node;
        }
        if (ch == '\\') {
            size_t escape = parser->pos;

            if (parser->pos >= parser->source_len) {
                break;
            }
            ch = (unsigned char)parser->source[parser->pos++];
            switch (ch) {
            case 'n':
                ch = '\n';
                break;
            case 't':
                ch = '\t';
                break;
            case 'r':
                ch = '\r';
                break;
            case '\\':
            case '\'':
            case '"':
            case '`':
                break;
            case '0':
                ch = '\0';
                break;
            default:
                parser->pos = escape + character_length(parser, escape);
                fail_parse_detail(parser, "invalid escape sequence \"\\%D\"",
                                  escape, parser->pos - escape);
                release_temporary(parser, decoded);
                return NULL;
            }
        }
        decoded[decoded_len++] = (char)ch;
    }
    fail_parse(parser, "unterminated string literal");
    release_temporary(parser, decoded);
    return NULL;
}

static reflow_expr_t *read_number_literal(expr_parser_t *parser)
{
    size_t start  = parser->pos;
    size_t digits = 0;
    char *text    = NULL;
    char *end     = NULL;
    double number = 0;

    if (parser->source[parser->pos] == '-') {
        parser->pos++;
    }
    while (parser->pos < parser->source_len &&
           is_digit((unsigned char)parser->source[parser->pos])) {
        parser->pos++;
        digits++;
    }
    if (digits == 0) {
        fail_parse_detail(parser, "invalid number literal \"%D\"", start,
                          parser->pos - start);
        return NULL;
    }
    if (parser->pos + 1 < parser->source_len &&
        parser->source[parser->pos] == '.' &&
        is_digit((unsigned char)parser->source[parser->pos + 1])) {
        parser->pos++;
        while (parser->pos < parser->source_len &&
               is_digit((unsigned char)parser->source[parser->pos])) {
            parser->pos++;
        }
    }
    if (parser->pos < parser->source_len &&
        (parser->source[parser->pos] == 'e' ||
         parser->source[parser->pos] == 'E')) {
        size_t exponent_digits = 0;

        parser->pos++;
        if (parser->pos < parser->source_len &&
            (parser->source[parser->pos] == '+' ||
             parser->source[parser->pos] == '-')) {
            parser->pos++;
        }
        while (parser->pos < parser->source_len &&
               is_digit((unsigned char)parser->source[parser->pos])) {
            parser->pos++;
            exponent_digits++;
        }
        if (exponent_digits == 0) {
            fail_parse_detail(parser, "invalid number literal \"%D\"", start,
                              parser->pos - start);
            return NULL;
        }
    }
    if (parser->pos - start == SIZE_MAX) {
        fail_oom(parser);
        return NULL;
    }
    text = (char *)pool_alloc(parser->temporary, parser->pos - start + 1);
    if (text == NULL) {
        fail_oom(parser);
        return NULL;
    }
    memcpy(text, parser->source + start, parser->pos - start);
    text[parser->pos - start] = '\0';
    number                    = strtod(text, &end);
    if (end != text + (parser->pos - start)) {
        release_temporary(parser, text);
        fail_parse_detail(parser, "invalid number literal \"%D\"", start,
                          parser->pos - start);
        return NULL;
    }
    {
        reflow_expr_t *node = NULL;

        release_temporary(parser, text);
        node = new_literal(parser, new_number_value(parser, number), start,
                           parser->pos);

        if (node == NULL) {
            fail_oom(parser);
        }
        return node;
    }
}

static reflow_expr_t *new_scope(expr_parser_t *parser, reflow_expr_type_t type,
                                size_t start, const char *name, size_t name_len)
{
    reflow_expr_t *node = new_expr(parser, type, start, parser->pos);

    if (node == NULL) {
        fail_oom(parser);
        return NULL;
    }
    if (name != NULL &&
        copy_string(parser, name, name_len, &node->as.scope.name) != 0) {
        fail_oom(parser);
        return NULL;
    }
    return node;
}

static reflow_expr_t *read_scope_reference(expr_parser_t *parser)
{
    size_t start      = parser->pos;
    size_t name_start = 0;
    size_t name_len   = 0;
    unsigned char ch  = (unsigned char)parser->source[parser->pos++];

    if (ch == '$') {
        if (parser->pos >= parser->source_len ||
            parser->source[parser->pos] != '.') {
            fail_parse(parser, "\"$\" must be followed by \".<identifier>\"");
            return NULL;
        }
        return new_scope(parser, REFLOW_EXPR_DOLLAR, start, NULL, 0);
    }
    if (ch == '@') {
        if (!read_identifier(parser, &name_start, &name_len)) {
            fail_parse(parser, "\"@\" must be followed by an identifier");
            return NULL;
        }
        return new_scope(parser, REFLOW_EXPR_AT, start,
                         parser->source + name_start, name_len);
    }
    if (parser->pos >= parser->source_len ||
        !is_identifier_start((unsigned char)parser->source[parser->pos])) {
        parser->pos = start;
        fail_parse(parser, "\".\" must be followed by an identifier");
        return NULL;
    }
    read_identifier(parser, &name_start, &name_len);
    return new_scope(parser, REFLOW_EXPR_DOT, start,
                     parser->source + name_start, name_len);
}

static reflow_expr_t *read_member_chain(expr_parser_t *parser,
                                        reflow_expr_t *node, size_t start)
{
    while (node != NULL) {
        size_t name_start = 0;
        size_t name_len   = 0;
        int optional      = 0;

        skip_space(parser);
        if (parser->pos + 1 < parser->source_len &&
            parser->source[parser->pos] == '?' &&
            parser->source[parser->pos + 1] == '.') {
            optional = 1;
            parser->pos += 2;
            if (!read_identifier(parser, &name_start, &name_len)) {
                fail_parse(parser, "expected identifier after \"?.\"");
                return NULL;
            }
        } else if (parser->pos + 1 < parser->source_len &&
                   parser->source[parser->pos] == '.' &&
                   is_identifier_start(
                       (unsigned char)parser->source[parser->pos + 1])) {
            parser->pos++;
            read_identifier(parser, &name_start, &name_len);
        } else {
            break;
        }
        node = new_member(parser, node, parser->source + name_start, name_len,
                          optional, start, parser->pos);
        if (node == NULL) {
            fail_oom(parser);
        }
    }
    return node;
}

static reflow_expr_t *read_computed_key(expr_parser_t *parser)
{
    reflow_expr_t *node = NULL;
    size_t start        = 0;
    unsigned char ch    = 0;

    skip_space(parser);
    if (parser->pos >= parser->source_len) {
        fail_parse(parser, "unexpected end of computed key");
        return NULL;
    }
    start = parser->pos;
    ch    = (unsigned char)parser->source[parser->pos];
    if (ch == '\'' || ch == '"') {
        return read_string_literal(parser);
    }
    if (is_digit(ch) ||
        (ch == '-' && parser->pos + 1 < parser->source_len &&
         is_digit((unsigned char)parser->source[parser->pos + 1]))) {
        return read_number_literal(parser);
    }
    if (ch != '$' && ch != '@' && ch != '.') {
        fail_parse(parser, COMPUTED_KEY_MESSAGE);
        return NULL;
    }
    node = read_scope_reference(parser);
    if (node == NULL) {
        return NULL;
    }
    return read_member_chain(parser, node, start);
}

static int read_object_key(expr_parser_t *parser, reflow_expr_t **result,
                           int *computed)
{
    size_t start       = 0;
    size_t name_start  = 0;
    size_t name_len    = 0;
    unsigned char ch   = 0;
    reflow_expr_t *key = NULL;

    skip_space(parser);
    if (parser->pos >= parser->source_len) {
        return fail_parse(
            parser,
            "expected object key (identifier, string, number, or [<expr>])");
    }
    start = parser->pos;
    ch    = (unsigned char)parser->source[parser->pos];
    if (ch == '[') {
        parser->pos++;
        key = read_computed_key(parser);
        if (key == NULL) {
            return -1;
        }
        skip_space(parser);
        if (parser->pos >= parser->source_len ||
            parser->source[parser->pos] != ']') {
            return fail_parse(parser, "expected \"]\"");
        }
        parser->pos++;
        *result   = key;
        *computed = 1;
        return 0;
    }
    if (ch == '\'' || ch == '"') {
        key = read_string_literal(parser);
    } else if (is_digit(ch) ||
               (ch == '-' && parser->pos + 1 < parser->source_len &&
                is_digit((unsigned char)parser->source[parser->pos + 1]))) {
        key = read_number_literal(parser);
    } else if (read_identifier(parser, &name_start, &name_len)) {
        key = new_literal(
            parser,
            new_string_value(parser, parser->source + name_start, name_len),
            start, parser->pos);
        if (key == NULL) {
            fail_oom(parser);
        }
    } else {
        return fail_parse(
            parser,
            "expected object key (identifier, string, number, or [<expr>])");
    }
    if (key == NULL) {
        return -1;
    }
    *result   = key;
    *computed = 0;
    return 0;
}

static int finish_array(expr_parser_t *parser, expr_frame_t *frame)
{
    reflow_expr_t *node =
        new_expr(parser, REFLOW_EXPR_ARRAY, frame->start, parser->pos);

    if (node == NULL) {
        return fail_oom(parser);
    }
    node->as.array = frame->list;
    complete_frame(parser, node);
    return 0;
}

static int finish_call(expr_parser_t *parser, expr_frame_t *frame)
{
    reflow_expr_t *node =
        new_expr(parser, REFLOW_EXPR_CALL, frame->start, parser->pos);

    if (node == NULL) {
        return fail_oom(parser);
    }
    node->as.call.callee    = frame->name;
    node->as.call.arguments = frame->list;
    complete_frame(parser, node);
    return 0;
}

static int finish_object(expr_parser_t *parser, expr_frame_t *frame)
{
    reflow_expr_t *node =
        new_expr(parser, REFLOW_EXPR_OBJECT, frame->start, parser->pos);

    if (node == NULL) {
        return fail_oom(parser);
    }
    node->as.object = frame->object;
    complete_frame(parser, node);
    return 0;
}

static int start_object_value(expr_parser_t *parser, expr_frame_t *frame)
{
    if (read_object_key(parser, &frame->key, &frame->computed) != 0) {
        return -1;
    }
    if (expect(parser, ':', "expected \":\"") != 0) {
        return -1;
    }
    frame->state = PRIMARY_OBJECT_VALUE;
    return push_frame(parser, EXPR_FRAME_TERNARY);
}

static int step_primary(expr_parser_t *parser, expr_frame_t *frame)
{
    if (frame->state == PRIMARY_GROUP) {
        reflow_expr_t *inner = parser->last;

        if (expect(parser, ')', "expected \")\"") != 0) {
            return -1;
        }
        complete_frame(parser, inner);
        return 0;
    }
    if (frame->state == PRIMARY_ARRAY_VALUE) {
        if (append_list(parser, &frame->list, parser->last) != 0) {
            return -1;
        }
        skip_space(parser);
        if (parser->pos < parser->source_len &&
            parser->source[parser->pos] == ',') {
            parser->pos++;
            skip_space(parser);
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == ']') {
                parser->pos++;
                return finish_array(parser, frame);
            }
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        if (expect(parser, ']', "expected \"]\"") != 0) {
            return -1;
        }
        return finish_array(parser, frame);
    }
    if (frame->state == PRIMARY_CALL_ARGUMENT) {
        if (append_list(parser, &frame->list, parser->last) != 0) {
            return -1;
        }
        skip_space(parser);
        if (parser->pos < parser->source_len &&
            parser->source[parser->pos] == ',') {
            parser->pos++;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        if (expect(parser, ')', "expected \")\"") != 0) {
            return -1;
        }
        return finish_call(parser, frame);
    }
    if (frame->state == PRIMARY_OBJECT_VALUE) {
        if (append_object_entry(parser, &frame->object, frame->key,
                                parser->last, frame->computed) != 0) {
            return -1;
        }
        skip_space(parser);
        if (parser->pos < parser->source_len &&
            parser->source[parser->pos] == ',') {
            parser->pos++;
            skip_space(parser);
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == '}') {
                parser->pos++;
                return finish_object(parser, frame);
            }
            return start_object_value(parser, frame);
        }
        if (expect(parser, '}', "expected \"}\"") != 0) {
            return -1;
        }
        return finish_object(parser, frame);
    }

    skip_space(parser);
    frame->start = parser->pos;
    if (parser->pos >= parser->source_len) {
        return fail_parse(parser, "unexpected end of expression");
    }
    {
        unsigned char ch = (unsigned char)parser->source[parser->pos];

        if (ch == '(') {
            parser->pos++;
            frame->state = PRIMARY_GROUP;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        if (ch == '[') {
            parser->pos++;
            skip_space(parser);
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == ']') {
                parser->pos++;
                return finish_array(parser, frame);
            }
            frame->state = PRIMARY_ARRAY_VALUE;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        if (ch == '{') {
            parser->pos++;
            skip_space(parser);
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == '}') {
                parser->pos++;
                return finish_object(parser, frame);
            }
            return start_object_value(parser, frame);
        }
        if (ch == '\'' || ch == '"') {
            reflow_expr_t *node = read_string_literal(parser);

            if (node == NULL) {
                return -1;
            }
            complete_frame(parser, node);
            return 0;
        }
        if (is_digit(ch) ||
            (ch == '-' && parser->pos + 1 < parser->source_len &&
             is_digit((unsigned char)parser->source[parser->pos + 1]))) {
            reflow_expr_t *node = read_number_literal(parser);

            if (node == NULL) {
                return -1;
            }
            complete_frame(parser, node);
            return 0;
        }
        if (ch == '$' || ch == '@' || ch == '.') {
            reflow_expr_t *node = read_scope_reference(parser);

            if (node == NULL) {
                return -1;
            }
            complete_frame(parser, node);
            return 0;
        }
        if (is_identifier_start(ch)) {
            size_t name_start       = 0;
            size_t name_len         = 0;
            reflow_value_t *literal = NULL;
            reflow_expr_t *node     = NULL;
            int is_keyword          = 0;

            read_identifier(parser, &name_start, &name_len);
            if (name_len == sizeof("true") - 1 &&
                memcmp(parser->source + name_start, "true", name_len) == 0) {
                literal    = new_bool_value(parser, 1);
                is_keyword = 1;
            } else if (name_len == sizeof("false") - 1 &&
                       memcmp(parser->source + name_start, "false", name_len) ==
                           0) {
                literal    = new_bool_value(parser, 0);
                is_keyword = 1;
            } else if (name_len == sizeof("null") - 1 &&
                       memcmp(parser->source + name_start, "null", name_len) ==
                           0) {
                literal    = new_value(parser, REFLOW_VALUE_NULL);
                is_keyword = 1;
            }
            if (is_keyword) {
                if (literal == NULL) {
                    return fail_oom(parser);
                }
                node = new_literal(parser, literal, frame->start, parser->pos);
                if (node == NULL) {
                    return fail_oom(parser);
                }
                complete_frame(parser, node);
                return 0;
            }
            skip_space(parser);
            if (parser->pos >= parser->source_len ||
                parser->source[parser->pos] != '(') {
                return fail_parse_detail(
                    parser,
                    "bare identifier \"%D\" is not a valid expression "
                    "(helpers must be called: \"%D(...)\")",
                    name_start, name_len);
            }
            if (copy_string(parser, parser->source + name_start, name_len,
                            &frame->name) != 0) {
                return fail_oom(parser);
            }
            parser->pos++;
            skip_space(parser);
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == ')') {
                parser->pos++;
                return finish_call(parser, frame);
            }
            frame->state = PRIMARY_CALL_ARGUMENT;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        return fail_parse_detail(parser, "unexpected character \"%D\"",
                                 parser->pos,
                                 character_length(parser, parser->pos));
    }
}

static int step_binary(expr_parser_t *parser, expr_frame_t *frame,
                       expr_frame_kind_t child, const char *operator_text,
                       size_t operator_len, reflow_expr_operator_t op)
{
    if (frame->state == 0) {
        frame->state = 1;
        return push_frame(parser, child);
    }
    if (frame->state == 1) {
        frame->first = parser->last;
        if (!consume(parser, operator_text, operator_len)) {
            complete_frame(parser, frame->first);
            return 0;
        }
        frame->state = 2;
        frame->op    = op;
        return push_frame(parser, child);
    }
    frame->first = new_binary(parser, frame->op, frame->first, parser->last,
                              frame->first->offset, parser->pos);
    if (frame->first == NULL) {
        return fail_oom(parser);
    }
    parser->last = frame->first;
    frame->state = 1;
    return 0;
}

static int read_comparison_operator(expr_parser_t *parser,
                                    reflow_expr_operator_t *op)
{
    static const struct {
        const char *text;
        reflow_expr_operator_t op;
    } operators[] = {
        {"==", REFLOW_EXPR_EQ},
        {"!=", REFLOW_EXPR_NE},
        {"<=", REFLOW_EXPR_LE},
        {">=", REFLOW_EXPR_GE},
        {"<",  REFLOW_EXPR_LT},
        {">",  REFLOW_EXPR_GT},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(operators) / sizeof(operators[0]); i++) {
        size_t len = strlen(operators[i].text);

        if (consume(parser, operators[i].text, len)) {
            *op = operators[i].op;
            return 1;
        }
    }
    return 0;
}

static int step_parser(expr_parser_t *parser)
{
    expr_frame_t *frame = &parser->frames[parser->depth - 1];

    switch (frame->kind) {
    case EXPR_FRAME_TERNARY:
        if (frame->state == 0) {
            skip_space(parser);
            frame->start = parser->pos;
            frame->state = 1;
            return push_frame(parser, EXPR_FRAME_COALESCE);
        }
        if (frame->state == 1) {
            frame->first = parser->last;
            if (!consume(parser, "?", 1)) {
                complete_frame(parser, frame->first);
                return 0;
            }
            frame->state = 2;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        if (frame->state == 2) {
            frame->second = parser->last;
            if (expect(parser, ':', "expected \":\"") != 0) {
                return -1;
            }
            frame->state = 3;
            return push_frame(parser, EXPR_FRAME_TERNARY);
        }
        {
            reflow_expr_t *node =
                new_ternary(parser, frame->first, frame->second, parser->last,
                            frame->start, parser->pos);

            if (node == NULL) {
                return fail_oom(parser);
            }
            complete_frame(parser, node);
            return 0;
        }

    case EXPR_FRAME_COALESCE:
        return step_binary(parser, frame, EXPR_FRAME_OR, "??", 2,
                           REFLOW_EXPR_COALESCE);
    case EXPR_FRAME_OR:
        return step_binary(parser, frame, EXPR_FRAME_AND, "||", 2,
                           REFLOW_EXPR_OR);
    case EXPR_FRAME_AND:
        return step_binary(parser, frame, EXPR_FRAME_COMPARISON, "&&", 2,
                           REFLOW_EXPR_AND);

    case EXPR_FRAME_COMPARISON:
        if (frame->state == 0) {
            frame->state = 1;
            return push_frame(parser, EXPR_FRAME_UNARY);
        }
        if (frame->state == 1) {
            frame->first = parser->last;
            if (!read_comparison_operator(parser, &frame->op)) {
                complete_frame(parser, frame->first);
                return 0;
            }
            frame->state = 2;
            return push_frame(parser, EXPR_FRAME_UNARY);
        }
        {
            reflow_expr_t *node =
                new_binary(parser, frame->op, frame->first, parser->last,
                           frame->first->offset, parser->pos);

            if (node == NULL) {
                return fail_oom(parser);
            }
            complete_frame(parser, node);
            return 0;
        }

    case EXPR_FRAME_UNARY:
        if (frame->state == 0) {
            skip_space(parser);
            frame->start = parser->pos;
            if (parser->pos < parser->source_len &&
                parser->source[parser->pos] == '!' &&
                (parser->pos + 1 >= parser->source_len ||
                 parser->source[parser->pos + 1] != '=')) {
                parser->pos++;
                frame->state = 1;
                return push_frame(parser, EXPR_FRAME_UNARY);
            }
            frame->state = 2;
            return push_frame(parser, EXPR_FRAME_POSTFIX);
        }
        if (frame->state == 1) {
            reflow_expr_t *node =
                new_unary(parser, REFLOW_EXPR_NOT, parser->last, frame->start,
                          parser->pos);

            if (node == NULL) {
                return fail_oom(parser);
            }
            complete_frame(parser, node);
            return 0;
        }
        complete_frame(parser, parser->last);
        return 0;

    case EXPR_FRAME_POSTFIX:
        if (frame->state == 0) {
            skip_space(parser);
            frame->start = parser->pos;
            frame->state = 1;
            return push_frame(parser, EXPR_FRAME_PRIMARY);
        }
        {
            reflow_expr_t *node =
                read_member_chain(parser, parser->last, frame->start);

            if (node == NULL) {
                return -1;
            }
            complete_frame(parser, node);
            return 0;
        }

    case EXPR_FRAME_PRIMARY:
        return step_primary(parser, frame);
    }
    return fail_parse(parser, "invalid expression parser state");
}

int reflow_expr_parse(pool_t *result_pool, pool_t *scratch_pool,
                      const char *source, size_t source_len,
                      reflow_expression_t **result, reflow_expr_error_t *error)
{
    reflow_expression_t *expr = NULL;
    char *source_copy         = NULL;
    int status                = -1;
    expr_parser_t parser      = {
        .result_pool = result_pool,
        .temporary   = scratch_pool,
        .source      = source,
        .source_len  = source_len,
        .error       = error,
    };

    if (result_pool == NULL || scratch_pool == NULL ||
        (source == NULL && source_len != 0) || result == NULL ||
        error == NULL) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    *error  = (reflow_expr_error_t){0};
    if (source_len == SIZE_MAX) {
        return fail_oom(&parser);
    }
    expr = (reflow_expression_t *)pool_calloc(result_pool, 1, sizeof(*expr));
    if (expr == NULL) {
        return fail_oom(&parser);
    }
    source_copy = (char *)pool_alloc(result_pool, source_len + 1);
    if (source_copy == NULL) {
        return fail_oom(&parser);
    }
    if (source_len != 0) {
        memcpy(source_copy, source, source_len);
    }
    source_copy[source_len] = '\0';
    expr->source            = (reflow_expr_string_t){
        .data = source_copy,
        .len  = source_len,
    };

    parser.source = source_copy;

    if (push_frame(&parser, EXPR_FRAME_TERNARY) != 0) {
        goto done;
    }
    while (parser.depth != 0) {
        if (step_parser(&parser) != 0) {
            goto done;
        }
    }
    skip_space(&parser);
    if (parser.pos != parser.source_len) {
        (void)fail_parse(&parser, "unexpected token");
        goto done;
    }
    expr->root = parser.last;
    status     = 0;

done:
    release_temporary(&parser, parser.frames);
    if (status == 0) {
        *result = expr;
    }
    return status;
}
