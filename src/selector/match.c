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
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */
#include "match.h"
#include <string.h>
#include "index.h"

/* True when c is ASCII whitespace as recognised by HTML class parsing. */
static int is_html_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Return true when `raw` (element's class attribute) contains the token
 * `cls` (length `cls_len`), matching HTML class-name equality. */
static bool class_contains(const char *raw, const char *cls, size_t cls_len)
{
    if (raw == NULL) return false;
    const char *s = raw;
    while (*s != '\0') {
        while (is_html_ws(*s)) s++;
        if (*s == '\0') break;
        const char *start = s;
        while (*s != '\0' && !is_html_ws(*s)) s++;
        if ((size_t)(s - start) == cls_len &&
            memcmp(start, cls, cls_len) == 0) {
            return true;
        }
    }
    return false;
}

bool sel_match_attr(const char *value, const sel_attr_cond *cond)
{
    /* Presence-only: any value including empty string satisfies. */
    if (cond->op == SEL_ATTR_OP_NONE) return value != NULL;
    if (value == NULL) return false;

    const char *target       = cond->value;
    size_t      target_len   = cond->value_len;
    size_t      value_len    = strlen(value);

    switch (cond->op) {
    case SEL_ATTR_OP_EQ:
        return value_len == target_len &&
               memcmp(value, target, target_len) == 0;

    case SEL_ATTR_OP_TILDE: {
        /* ~= : whitespace-separated token equality. An empty target or
         * one that contains whitespace can never match — CSS Level 3
         * pins this shape to a single word. */
        if (target_len == 0) return false;
        for (size_t i = 0; i < target_len; i++) {
            if (is_html_ws(target[i])) return false;
        }
        return class_contains(value, target, target_len);
    }

    case SEL_ATTR_OP_PIPE: {
        /* |= : equal to target OR starts with `target-`. */
        if (value_len == target_len &&
            memcmp(value, target, target_len) == 0) {
            return true;
        }
        if (value_len > target_len &&
            memcmp(value, target, target_len) == 0 &&
            value[target_len] == '-') {
            return true;
        }
        return false;
    }

    case SEL_ATTR_OP_CARET:
        return target_len > 0 && value_len >= target_len &&
               memcmp(value, target, target_len) == 0;

    case SEL_ATTR_OP_DOLLAR:
        return target_len > 0 && value_len >= target_len &&
               memcmp(value + value_len - target_len, target,
                      target_len) == 0;

    case SEL_ATTR_OP_STAR: {
        if (target_len == 0 || target_len > value_len) return false;
        for (size_t i = 0; i + target_len <= value_len; i++) {
            if (memcmp(value + i, target, target_len) == 0) return true;
        }
        return false;
    }
    default:
        return false;
    }
}

bool sel_match_compound(const ir_node *el, const sel_compound *compound)
{
    if (el == NULL || el->type != IR_ELEMENT) return false;

    if (compound->tag != NULL) {
        if (el->element.tag_name == NULL) return false;
        if (strlen(el->element.tag_name) != compound->tag_len) return false;
        if (memcmp(el->element.tag_name, compound->tag,
                   compound->tag_len) != 0) return false;
    }

    if (compound->id != NULL) {
        const char *v = sel_static_attr(el, "id");
        if (v == NULL) return false;
        if (strlen(v) != compound->id_len) return false;
        if (memcmp(v, compound->id, compound->id_len) != 0) return false;
    }

    if (compound->n_classes > 0) {
        const char *raw = sel_static_attr(el, "class");
        if (raw == NULL) return false;
        for (size_t i = 0; i < compound->n_classes; i++) {
            if (!class_contains(raw, compound->classes[i],
                                compound->class_lens[i])) return false;
        }
    }

    for (size_t i = 0; i < compound->n_attrs; i++) {
        const sel_attr_cond *a = &compound->attrs[i];
        /* sel_static_attr keys are case-sensitive. Attribute names in
         * the compound have already been lowercased by the parser to
         * match HTML case-insensitive attribute lookup, so this stays
         * consistent as long as element attribute names are also stored
         * lowercase (compile.c ensures that today). */
        const char *v = sel_static_attr(el, a->name);
        if (!sel_match_attr(v, a)) return false;
    }

    return true;
}
