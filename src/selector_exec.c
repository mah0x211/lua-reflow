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

#include "reflow_util.h"
#include "selector_exec.h"

static int bytes_equal(const char *a, size_t alen, const char *b, size_t blen)
{
    return alen == blen && (alen == 0 || memcmp(a, b, alen) == 0);
}

/* Report whether s holds a character of the JavaScript \s class. */
static int has_space(const char *s, size_t len)
{
    size_t pos = 0;

    for (pos = 0; pos < len; pos++) {
        if (reflow_js_space_len(s, len, pos) > 0) {
            return 1;
        }
    }
    return 0;
}

/* Report whether token is one of the names of s split as s.split(/\s+/)
 * splits it in JavaScript. token is never empty. */
static int has_token(const char *s, size_t len, const char *token,
                     size_t token_len)
{
    size_t pos = 0;

    while (pos < len) {
        size_t start = 0;
        size_t step  = reflow_js_space_len(s, len, pos);

        if (step > 0) {
            pos += step;
            continue;
        }
        start = pos;
        while (pos < len && reflow_js_space_len(s, len, pos) == 0) {
            pos++;
        }
        if (bytes_equal(s + start, pos - start, token, token_len)) {
            return 1;
        }
    }
    return 0;
}

static int has_substring(const char *s, size_t len, const char *sub,
                         size_t sub_len)
{
    size_t i = 0;

    for (i = 0; sub_len <= len && i <= len - sub_len; i++) {
        if (memcmp(s + i, sub, sub_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Return the value of the first static attribute named name, as the
 * JavaScript getStaticAttr does, or NULL when the element has none. */
static const ir_string_t *static_attr(const ir_element_t *element,
                                      const char *name, size_t len)
{
    const ir_attr_t *attr = NULL;

    for (attr = element->attrs.head; attr != NULL; attr = attr->next) {
        if (bytes_equal(attr->name.data, attr->name.len, name, len)) {
            return &attr->value;
        }
    }
    return NULL;
}

/* Report whether value, the first static attribute of the name of cond or
 * NULL when there is none, satisfies cond, as the JavaScript matchAttr does. */
static int match_attr(const ir_string_t *value, const selector_attr_t *cond)
{
    const char *target = cond->value.data;
    size_t tlen        = cond->value.len;

    if (value == NULL) {
        return 0;
    }
    switch (cond->op) {
    case SELECTOR_ATTR_EXISTS:
        return 1;
    case SELECTOR_ATTR_EQUAL:
        return bytes_equal(value->data, value->len, target, tlen);
    case SELECTOR_ATTR_INCLUDES:
        return tlen > 0 && !has_space(target, tlen) &&
               has_token(value->data, value->len, target, tlen);
    case SELECTOR_ATTR_DASH:
        return bytes_equal(value->data, value->len, target, tlen) ||
               (value->len > tlen && value->data[tlen] == '-' &&
                (tlen == 0 || memcmp(value->data, target, tlen) == 0));
    case SELECTOR_ATTR_PREFIX:
        return tlen > 0 && value->len >= tlen &&
               memcmp(value->data, target, tlen) == 0;
    case SELECTOR_ATTR_SUFFIX:
        return tlen > 0 && value->len >= tlen &&
               memcmp(value->data + value->len - tlen, target, tlen) == 0;
    case SELECTOR_ATTR_SUBSTRING:
        break;
    }
    return tlen > 0 && has_substring(value->data, value->len, target, tlen);
}

/* Report whether element satisfies every non-positional condition of
 * compound, as the JavaScript matchCompound does. */
static int match_compound(const ir_element_t *element,
                          const selector_compound_t *compound)
{
    const ir_string_t *value = NULL;
    size_t i                 = 0;

    if (compound->tag.data != NULL &&
        !bytes_equal(element->tag_name.data, element->tag_name.len,
                     compound->tag.data, compound->tag.len)) {
        return 0;
    }
    if (compound->id.data != NULL) {
        value = static_attr(element, "id", 2);
        if (value == NULL ||
            !bytes_equal(value->data, value->len, compound->id.data,
                         compound->id.len)) {
            return 0;
        }
    }
    if (compound->nclasses > 0) {
        value = static_attr(element, "class", 5);
        for (i = 0; i < compound->nclasses; i++) {
            const selector_string_t *cls = &compound->classes[i];

            if (value == NULL ||
                !has_token(value->data, value->len, cls->data, cls->len)) {
                return 0;
            }
        }
    }
    for (i = 0; i < compound->nattrs; i++) {
        const selector_attr_t *cond = &compound->attrs[i];

        if (!match_attr(static_attr(element, cond->name.data, cond->name.len),
                        cond)) {
            return 0;
        }
    }
    return 1;
}

/* Report whether element matches the rightmost part of complex and has
 * ancestors that match the preceding parts. A child combinator moves one
 * level up; a descendant combinator moves up to the nearest matching
 * ancestor. */
static int match_complex(const ir_element_t *element,
                         const selector_complex_t *complex)
{
    const ir_element_t *current = NULL;
    size_t i                    = complex->nparts - 1;

    if (!match_compound(element, &complex->parts[i].compound)) {
        return 0;
    }
    current = element->structure.parent;
    while (i > 0) {
        const selector_compound_t *compound = &complex->parts[i - 1].compound;

        if (complex->parts[i].combinator == SELECTOR_COMBINATOR_CHILD) {
            if (current == NULL || !match_compound(current, compound)) {
                return 0;
            }
        } else {
            while (current != NULL && !match_compound(current, compound)) {
                current = current->structure.parent;
            }
            if (current == NULL) {
                return 0;
            }
        }
        current = current->structure.parent;
        i--;
    }
    return 1;
}

/* Keep the shorter of best and the list indexed under key on axis. Return 0
 * when the key is not indexed, which leaves no element to seed. */
static int keep_shorter(const ir_index_axis_t *axis,
                        const selector_string_t *key,
                        const ir_element_list_t **best)
{
    const ir_element_list_t *list = ir_index_find(axis, key->data, key->len);

    if (list == NULL) {
        return 0;
    } else if (*best == NULL || list->count < (*best)->count) {
        *best = list;
    }
    return 1;
}

/* Return the seed list of complex: the elements indexed under the id, the
 * class with the fewest elements, the tag, or the attribute name with the
 * fewest elements of its rightmost compound, in this order, or every element
 * when the compound has none of them. NULL means no element. */
static const ir_element_list_t *seed(const ir_dom_index_t *index,
                                     const selector_complex_t *complex)
{
    const selector_compound_t *last = selector_exec_rightmost(complex);
    const ir_element_list_t *best   = NULL;
    size_t i                        = 0;

    if (last->id.data != NULL) {
        return ir_index_find(&index->by_id, last->id.data, last->id.len);
    } else if (last->nclasses > 0) {
        for (i = 0; i < last->nclasses; i++) {
            if (!keep_shorter(&index->by_class, &last->classes[i], &best)) {
                return NULL;
            }
        }
        return best;
    } else if (last->tag.data != NULL) {
        return ir_index_find(&index->by_tag, last->tag.data, last->tag.len);
    } else if (last->nattrs > 0) {
        for (i = 0; i < last->nattrs; i++) {
            if (!keep_shorter(&index->by_attr_name, &last->attrs[i].name,
                              &best)) {
                return NULL;
            }
        }
        return best;
    }
    return &index->elements;
}

/* Return the first positional condition of a part other than the rightmost
 * one, or NULL. */
static const selector_pseudo_t *
ancestor_pseudo(const selector_complex_t *complex)
{
    size_t i = 0;

    for (i = 0; i + 1 < complex->nparts; i++) {
        if (complex->parts[i].compound.npseudos > 0) {
            return &complex->parts[i].compound.pseudos[0];
        }
    }
    return NULL;
}

int selector_exec_resolve(pool_t *pool, const ir_dom_index_t *index,
                          const selector_t *selector,
                          selector_candidates_t *out,
                          const selector_pseudo_t **ancestor)
{
    size_t nelements                   = index->elements.count;
    const selector_complex_t **matched = NULL;
    selector_candidate_t *items        = NULL;
    size_t count                       = 0;
    size_t i                           = 0;
    size_t j                           = 0;

    if (nelements == 0) {
        *out = (selector_candidates_t){NULL, 0};
        return 0;
    }
    /* The complex selector matched by each element, by document order. */
    matched = (const selector_complex_t **)pool_calloc(pool, nelements,
                                                       sizeof(*matched));
    if (matched == NULL) {
        return -1;
    }
    for (i = 0; i < selector->ncomplexes; i++) {
        const selector_complex_t *complex = &selector->complexes[i];
        const ir_element_list_t *seeds    = seed(index, complex);

        for (j = 0; seeds != NULL && j < seeds->count; j++) {
            const ir_element_t *element = seeds->items[j];
            size_t order                = element->structure.order;

            if (!match_complex(element, complex)) {
                continue;
            } else if ((*ancestor = ancestor_pseudo(complex)) != NULL) {
                pool_free(pool, (void *)matched);
                errno = EINVAL;
                return -1;
            } else if (matched[order] == NULL) {
                matched[order] = complex;
                count++;
            }
        }
    }
    if (count > 0) {
        items =
            (selector_candidate_t *)pool_calloc(pool, count, sizeof(*items));
        if (items == NULL) {
            pool_free(pool, (void *)matched);
            return -1;
        }
        for (i = 0, j = 0; i < nelements; i++) {
            if (matched[i] != NULL) {
                items[j++] = (selector_candidate_t){index->elements.items[i],
                                                    matched[i]};
            }
        }
    }
    pool_free(pool, (void *)matched);
    *out = (selector_candidates_t){items, count};
    return 0;
}
